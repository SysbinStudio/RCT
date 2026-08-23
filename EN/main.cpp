#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <vector>
#include <fstream>
#include <sys/types.h>
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
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
    std::cerr << "[!] " << msg << std::endl;
    exit(1);
}

#ifdef _WIN32
void init_winsock() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        error("WSAStartup failed");
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
    uint32_t len = htonl(s.size());
    if (!send_all(sock, (char*)&len, sizeof(len))) return false;
    if (s.size() > 0 && !send_all(sock, s.c_str(), s.size())) return false;
    return true;
}

bool recv_string(int sock, std::string& out) {
    uint32_t len;
    if (!recv_all(sock, (char*)&len, sizeof(len))) return false;
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

void handle_client(int client_sock) {
    std::cout << "[+] Client connected" << std::endl;
    while (true) {
        std::string cmd_type;
        if (!recv_string(client_sock, cmd_type)) {
            std::cout << "[-] Client disconnected" << std::endl;
            break;
        }

        if (cmd_type == "CMD") {
            std::string command;
            if (!recv_string(client_sock, command)) break;

#ifdef _WIN32
            FILE* pipe = _popen(command.c_str(), "r");
#else
            FILE* pipe = popen(command.c_str(), "r");
#endif
            std::string result;
            if (pipe) {
                char buf[BUFFER_SIZE];
                while (fgets(buf, sizeof(buf), pipe) != nullptr) {
                    result += buf;
                }
#ifdef _WIN32
                _pclose(pipe);
#else
                pclose(pipe);
#endif
            } else {
                result = "[ERROR] Cannot execute command";
            }
            send_string(client_sock, result);
        }
        else if (cmd_type == "SENDFILE") {
            std::string filename;
            if (!recv_string(client_sock, filename)) break;
            uint64_t filesize;
            if (!recv_all(client_sock, (char*)&filesize, sizeof(filesize))) break;
            filesize = be64toh(filesize);

            std::ofstream outfile(filename, std::ios::binary);
            if (!outfile) {
                send_string(client_sock, "FAIL");
                break;
            }
            send_string(client_sock, "OK");
            char buf[BUFFER_SIZE];
            uint64_t remaining = filesize;
            while (remaining > 0) {
                int n = recv(client_sock, buf, std::min((uint64_t)BUFFER_SIZE, remaining), 0);
                if (n <= 0) break;
                outfile.write(buf, n);
                remaining -= n;
            }
            outfile.close();
            std::cout << "[+] Received file: " << filename << " (" << filesize << " bytes)" << std::endl;
        }
        else if (cmd_type == "RECVFILE") {
            std::string remote_path;
            if (!recv_string(client_sock, remote_path)) break;

            std::ifstream infile(remote_path, std::ios::binary | std::ios::ate);
            if (!infile) {
                send_string(client_sock, "FAIL");
                continue;
            }
            uint64_t filesize = infile.tellg();
            infile.seekg(0);
            send_string(client_sock, "OK");
            uint64_t size_be = htobe64(filesize);
            send_all(client_sock, (char*)&size_be, sizeof(size_be));
            char buf[BUFFER_SIZE];
            while (filesize > 0) {
                int n = std::min((uint64_t)BUFFER_SIZE, filesize);
                infile.read(buf, n);
                send_all(client_sock, buf, n);
                filesize -= n;
            }
            infile.close();
            std::cout << "[+] Sent file: " << remote_path << std::endl;
        }
        else {
            send_string(client_sock, "UNKNOWN");
        }
    }
    close_socket(client_sock);
}

void server_mode() {
#ifdef _WIN32
    init_winsock();
#endif
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) error("Socket creation failed");

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        error("Bind failed");

    if (listen(server_sock, 5) < 0)
        error("Listen failed");

    std::cout << "[*] Server started, listening on port " << PORT << std::endl;
    std::cout << "[*] Waiting for client connection..." << std::endl;

    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &len);
    if (client_sock < 0) error("Accept failed");

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::cout << "[+] Client connected from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

    std::thread client_thread(handle_client, client_sock);
    client_thread.detach();

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
                std::cout << "Usage: cmd <command>" << std::endl;
                continue;
            }
            cmd = cmd.substr(1);
            send_string(client_sock, "CMD");
            send_string(client_sock, cmd);
            std::string result;
            recv_string(client_sock, result);
            std::cout << result << std::endl;
        }
        else if (op == "sendfile") {
            std::string local_path, remote_path;
            iss >> local_path >> remote_path;
            if (local_path.empty() || remote_path.empty()) {
                std::cout << "Usage: sendfile <local_path> <remote_path>" << std::endl;
                continue;
            }
            std::ifstream infile(local_path, std::ios::binary | std::ios::ate);
            if (!infile) {
                std::cout << "[!] Cannot open local file: " << local_path << std::endl;
                continue;
            }
            uint64_t filesize = infile.tellg();
            infile.seekg(0);

            send_string(client_sock, "SENDFILE");
            send_string(client_sock, remote_path);
            uint64_t size_be = htobe64(filesize);
            send_all(client_sock, (char*)&size_be, sizeof(size_be));
            std::string ack;
            recv_string(client_sock, ack);
            if (ack != "OK") {
                std::cout << "[!] Client refused to receive" << std::endl;
                continue;
            }
            char buf[BUFFER_SIZE];
            while (filesize > 0) {
                int n = std::min((uint64_t)BUFFER_SIZE, filesize);
                infile.read(buf, n);
                send_all(client_sock, buf, n);
                filesize -= n;
            }
            infile.close();
            std::cout << "[+] File sent successfully" << std::endl;
        }
        else if (op == "recvfile") {
            std::string remote_path, local_path;
            iss >> remote_path >> local_path;
            if (remote_path.empty() || local_path.empty()) {
                std::cout << "Usage: recvfile <remote_path> <local_path>" << std::endl;
                continue;
            }
            send_string(client_sock, "RECVFILE");
            send_string(client_sock, remote_path);
            std::string ack;
            recv_string(client_sock, ack);
            if (ack != "OK") {
                std::cout << "[!] Remote file not found or unreadable" << std::endl;
                continue;
            }
            uint64_t filesize;
            recv_all(client_sock, (char*)&filesize, sizeof(filesize));
            filesize = be64toh(filesize);
            std::ofstream outfile(local_path, std::ios::binary);
            if (!outfile) {
                std::cout << "[!] Cannot create local file: " << local_path << std::endl;
                continue;
            }
            char buf[BUFFER_SIZE];
            uint64_t remaining = filesize;
            while (remaining > 0) {
                int n = recv(client_sock, buf, std::min((uint64_t)BUFFER_SIZE, remaining), 0);
                if (n <= 0) break;
                outfile.write(buf, n);
                remaining -= n;
            }
            outfile.close();
            std::cout << "[+] File received (" << filesize << " bytes)" << std::endl;
        }
        else {
            std::cout << "Available commands: cmd, sendfile, recvfile, exit" << std::endl;
        }
    }
    close_socket(client_sock);
    close_socket(server_sock);
#ifdef _WIN32
    WSACleanup();
#endif
}

void client_mode(const std::string& server_ip) {
#ifdef _WIN32
    init_winsock();
#endif
    int client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock < 0) error("Socket creation failed");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr) <= 0)
        error("Invalid server IP");

    if (connect(client_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        error("Connection failed");

    std::cout << "[+] Connected to server " << server_ip << ":" << PORT << std::endl;
    std::cout << "[*] Waiting for commands..." << std::endl;

    handle_client(client_sock);

    close_socket(client_sock);
#ifdef _WIN32
    WSACleanup();
#endif
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " --server | --client [server_ip] | --version" << std::endl;
        return 0;
    }

    std::string arg = argv[1];
    if (arg == "--version") {
        std::cout << "Remote Control Tool version " << VERSION << std::endl;
        return 0;
    }
    else if (arg == "--server") {
        server_mode();
    }
    else if (arg == "--client") {
        if (argc < 3) {
            std::cout << "Please specify server IP: " << argv[0] << " --client <server_ip>" << std::endl;
            return 1;
        }
        client_mode(argv[2]);
    }
    else {
        std::cout << "Unknown argument: " << arg << std::endl;
        return 1;
    }
    return 0;
}
