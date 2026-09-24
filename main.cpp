#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <vector>
#include <fstream>
#include <algorithm>
#include <sys/types.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
    #pragma comment(lib, "ws2_32.lib")

    // ==================== Windows 字节序转换兼容补丁 ====================
    static inline uint64_t htonll_compat(uint64_t val) {
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_bswap64(val);
        #elif defined(_MSC_VER)
            return _byteswap_uint64(val);
        #else
            return ((uint64_t)htonl((uint32_t)(val >> 32)) |
                    ((uint64_t)htonl((uint32_t)val) << 32));
        #endif
    }
    static inline uint64_t ntohll_compat(uint64_t val) { return htonll_compat(val); }
    #define htobe64(x) htonll_compat(x)
    #define be64toh(x) ntohll_compat(x)

    // ==================== Windows UTF-8 控制台强制初始化 ====================
    static void setup_utf8_console() {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
    }
#endif

#ifndef _WIN32
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <endian.h>
#endif

const int BUFFER_SIZE = 4096;
const int PORT = 8888;
const std::string VERSION = "1.0";

void close_socket(int sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

void error(const std::string& msg) {
    std::cerr << "[!] " << msg << "\r\n";
    exit(1);
}

#ifdef _WIN32
void init_winsock() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) error("WSAStartup failed");
}
#endif

bool send_all(int sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool recv_all(int sock, char* buf, size_t len) {
    size_t received = 0;
    while (received < len) {
        int n = recv(sock, buf + received, len - received, 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool send_string(int sock, const std::string& s) {
    uint32_t len = htonl(static_cast<uint32_t>(s.size()));
    if (!send_all(sock, reinterpret_cast<const char*>(&len), sizeof(len))) return false;
    if (s.size() > 0 && !send_all(sock, s.c_str(), s.size())) return false;
    return true;
}

bool recv_string(int sock, std::string& out) {
    uint32_t len;
    if (!recv_all(sock, reinterpret_cast<char*>(&len), sizeof(len))) return false;
    len = ntohl(len);
    if (len > 0) {
        std::vector<char> buf(len + 1);
        if (!recv_all(sock, buf.data(), len)) return false;
        buf[len] = '\0';
        out.assign(buf.data());
    } else {
        out.clear();
    }
    return true;
}

// ==================== 核心逻辑：处理接收到的指令（原 handle_client 改名） ====================
// 这个函数现在只被 Client 端调用，用来响应 Server 发来的命令
void process_incoming_commands(int sock) {
    while (true) {
        std::string cmd_type;
        if (!recv_string(sock, cmd_type)) {
            std::cout << "[-] 与服务端断开连接\r\n";
            break;
        }

        if (cmd_type == "CMD") {
            std::string command;
            if (!recv_string(sock, command)) break;

#ifdef _WIN32
            FILE* pipe = _popen(command.c_str(), "r");
#else
            FILE* pipe = popen(command.c_str(), "r");
#endif
            std::string result;
            if (pipe) {
                char buf[BUFFER_SIZE];
                while (fgets(buf, sizeof(buf), pipe) != nullptr) result += buf;
#ifdef _WIN32
                _pclose(pipe);
#else
                pclose(pipe);
#endif
            } else {
                result = "[ERROR] Cannot execute command";
            }
            // 【关键】执行完必须把结果发回去，否则 Server 会永久阻塞在 recv_string
            send_string(sock, result);
        }
        else if (cmd_type == "SENDFILE") {
            std::string filename;
            if (!recv_string(sock, filename)) break;

            uint64_t filesize;
            if (!recv_all(sock, reinterpret_cast<char*>(&filesize), sizeof(filesize))) break;
            filesize = be64toh(filesize);

            std::ofstream outfile(filename, std::ios::binary);
            if (!outfile) {
                send_string(sock, "FAIL");
                break;
            }
            send_string(sock, "OK");

            char buf[BUFFER_SIZE];
            uint64_t remaining = filesize;
            while (remaining > 0) {
                size_t to_recv = std::min(static_cast<uint64_t>(BUFFER_SIZE), remaining);
                int n = recv(sock, buf, static_cast<int>(to_recv), 0);
                if (n <= 0) break;
                outfile.write(buf, n);
                remaining -= n;
            }
            outfile.close();
            std::cout << "[+] 收到文件: " << filename << " (" << filesize << " bytes)\r\n";
        }
        else if (cmd_type == "RECVFILE") {
            std::string remote_path;
            if (!recv_string(sock, remote_path)) break;

            std::ifstream infile(remote_path, std::ios::binary | std::ios::ate);
            if (!infile) {
                send_string(sock, "FAIL");
                continue;
            }
            uint64_t filesize = static_cast<uint64_t>(infile.tellg());
            infile.seekg(0);
            send_string(sock, "OK");

            uint64_t size_be = htobe64(filesize);
            send_all(sock, reinterpret_cast<const char*>(&size_be), sizeof(size_be));

            char buf[BUFFER_SIZE];
            while (filesize > 0) {
                size_t to_send = std::min(static_cast<uint64_t>(BUFFER_SIZE), filesize);
                infile.read(buf, static_cast<std::streamsize>(to_send));
                send_all(sock, buf, static_cast<int>(to_send));
                filesize -= to_send;
            }
            infile.close();
            std::cout << "[+] 发送文件: " << remote_path << "\r\n";
        }
        else {
            send_string(sock, "UNKNOWN");
        }
    }
    close_socket(sock);
}

// ==================== 服务端模式（主控端） ====================
void server_mode() {
#ifdef _WIN32
    init_winsock();
    setup_utf8_console();
#endif
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) error("Socket creation failed");

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) error("Bind failed");
    if (listen(server_sock, 5) < 0) error("Listen failed");

    std::cout << "[*] 服务端启动，监听端口 " << PORT << "\r\n";
    std::cout << "[*] 等待客户端连接...\r\n";

    struct sockaddr_in client_addr{};
    socklen_t len = sizeof(client_addr);
    int client_sock = accept(server_sock, reinterpret_cast<struct sockaddr*>(&client_addr), &len);
    if (client_sock < 0) error("Accept failed");

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::cout << "[+] 客户端连接来自 " << client_ip << ":" << ntohs(client_addr.sin_port) << "\r\n";

    // 【修复】不再 detach 线程，由主线程同步处理交互
    std::string input;
    while (true) {
        std::cout << "remote> ";
        std::getline(std::cin, input);
        if (input == "exit" || input == "quit") break;

        std::istringstream iss(input);
        std::string op;
        iss >> op;

        if (op == "cmd") {
            std::string cmd;
            std::getline(iss, cmd);
            if (cmd.empty()) {
                std::cout << "用法: cmd <command>\r\n";
                continue;
            }
            cmd = cmd.substr(1); 
            
            send_string(client_sock, "CMD");
            send_string(client_sock, cmd);
            
            // 【核心修复】发送后必须接收结果，形成闭环
            std::string result;
            if (recv_string(client_sock, result)) {
                std::cout << result << "\r\n";
            } else {
                std::cout << "[!] 读取客户端响应失败，连接可能已断开\r\n";
                break;
            }
        }
        else if (op == "sendfile") {
            std::string local_path, remote_path;
            iss >> local_path >> remote_path;
            if (local_path.empty() || remote_path.empty()) {
                std::cout << "用法: sendfile <local_path> <remote_path>\r\n";
                continue;
            }
            std::ifstream infile(local_path, std::ios::binary | std::ios::ate);
            if (!infile) {
                std::cout << "[!] 无法打开本地文件: " << local_path << "\r\n";
                continue;
            }
            uint64_t filesize = static_cast<uint64_t>(infile.tellg());
            infile.seekg(0);

            send_string(client_sock, "SENDFILE");
            send_string(client_sock, remote_path);

            uint64_t size_be = htobe64(filesize);
            send_all(client_sock, reinterpret_cast<const char*>(&size_be), sizeof(size_be));

            std::string ack;
            recv_string(client_sock, ack);
            if (ack != "OK") {
                std::cout << "[!] 客户端拒绝接收\r\n";
                continue;
            }

            char buf[BUFFER_SIZE];
            while (filesize > 0) {
                size_t to_send = std::min(static_cast<uint64_t>(BUFFER_SIZE), filesize);
                infile.read(buf, static_cast<std::streamsize>(to_send));
                send_all(client_sock, buf, static_cast<int>(to_send));
                filesize -= to_send;
            }
            infile.close();
            std::cout << "[+] 文件发送完成\r\n";
        }
        else if (op == "recvfile") {
            std::string remote_path, local_path;
            iss >> remote_path >> local_path;
            if (remote_path.empty() || local_path.empty()) {
                std::cout << "用法: recvfile <remote_path> <local_path>\r\n";
                continue;
            }
            send_string(client_sock, "RECVFILE");
            send_string(client_sock, remote_path);

            std::string ack;
            recv_string(client_sock, ack);
            if (ack != "OK") {
                std::cout << "[!] 客户端文件不存在或无法读取\r\n";
                continue;
            }

            uint64_t filesize;
            recv_all(client_sock, reinterpret_cast<char*>(&filesize), sizeof(filesize));
            filesize = be64toh(filesize);

            std::ofstream outfile(local_path, std::ios::binary);
            if (!outfile) {
                std::cout << "[!] 无法创建本地文件: " << local_path << "\r\n";
                continue;
            }
            char buf[BUFFER_SIZE];
            uint64_t remaining = filesize;
            while (remaining > 0) {
                size_t to_recv = std::min(static_cast<uint64_t>(BUFFER_SIZE), remaining);
                int n = recv(client_sock, buf, static_cast<int>(to_recv), 0);
                if (n <= 0) break;
                outfile.write(buf, n);
                remaining -= n;
            }
            outfile.close();
            std::cout << "[+] 文件接收完成 (" << filesize << " bytes)\r\n";
        }
        else {
            std::cout << "可用命令: cmd, sendfile, recvfile, exit\r\n";
        }
    }
    close_socket(client_sock);
    close_socket(server_sock);
#ifdef _WIN32
    WSACleanup();
#endif
}

// ==================== 客户端模式（被控端） ====================
void client_mode(const std::string& server_ip) {
#ifdef _WIN32
    init_winsock();
    setup_utf8_console();
#endif
    int client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock < 0) error("Socket creation failed");

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr) <= 0) error("Invalid server IP");

    if (connect(client_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) error("Connection failed");

    std::cout << "[+] 已连接到服务端 " << server_ip << ":" << PORT << "\r\n";
    std::cout << "[*] 等待控制命令...\r\n";

    // 【修复】客户端进入纯接收循环，等待服务端下发指令
    process_incoming_commands(client_sock);

    close_socket(client_sock);
#ifdef _WIN32
    WSACleanup();
#endif
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif

    if (argc < 2) {
        std::cout << "用法: " << argv[0] << " --server | --client [server_ip] | --version\r\n";
        return 0;
    }

    std::string arg = argv[1];
    if (arg == "--version") {
        std::cout << "Remote Control Tool version " << VERSION << "\r\n";
        return 0;
    }
    else if (arg == "--server") {
        server_mode();
    }
    else if (arg == "--client") {
        if (argc < 3) {
            std::cout << "请指定服务端 IP: " << argv[0] << " --client <server_ip>\r\n";
            return 1;
        }
        client_mode(argv[2]);
    }
    else {
        std::cout << "未知参数: " << arg << "\r\n";
        return 1;
    }
    return 0;
}
