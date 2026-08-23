Remote Control Tool

A cross-platform remote management tool developed in C++, supporting Windows and Linux/macOS systems. This tool allows users to execute remote commands, upload, and download files via a command-line interface.

Features

- Cross-platform support: Compatible with Windows (Winsock) and Unix-like systems (POSIX sockets).
- Remote Command Execution: Execute system commands on the server or client and return results.
- File Transfer:
  - sendfile: Send files from local to remote host.
  - recvfile: Receive files from remote host to local.
- Binary-safe transmission: Uses length-prefixed protocol to ensure complete data reception.
- Multi-threaded processing: The server uses independent threads to handle client connections, keeping the main control loop responsive.

Compilation Requirements

- C++11 or higher compiler (GCC, Clang, MSVC)
- CMake or Makefile (optional, can also use compiler commands directly)

Linux/macOS Compilation

g++ -std=c++11 -o rct main.cpp -pthread

Windows Compilation (MSVC)

cl /EHsc main.cpp ws2_32.lib

Usage

Start Server

Run the following command on the target machine to start the listening service (default port 8888):

./rct --server

Start Client

Run the following command on the controlling machine to connect to the server:

./rct --client <Server_IP>

Example:

./rct --client 192.168.1.100

Check Version

./rct --version

Control Commands

After successful connection, enter the following commands in the client console to operate:

Command | Description | Example
--- | --- | ---
cmd | Execute remote system command | cmd ls -la or cmd dir
sendfile | Upload file to remote host | sendfile local.txt remote/path/file.txt
recvfile | Download file from remote host | recvfile remote/path/file.txt local.txt
exit / quit | Disconnect and exit program | exit

Command Details

1. Execute Command (cmd)

Execute specified Shell or CMD commands on the remote host and print the output to the local console.

remote> cmd whoami

2. Send File (sendfile)

Upload local files to the specified path on the remote host.

remote> sendfile ./photo.jpg /tmp/photo.jpg

3. Receive File (recvfile)

Download files from the specified path on the remote host to local.

remote> recvfile /var/log/syslog ./syslog_backup.log

Technical Details

- Communication Protocol: TCP
- Default Port: 8888
- Data Encoding:
  - String transmission uses uint32_t network byte order length prefix + raw content.
  - File size transmission uses uint64_t network byte order.
- Security Warning:
  - This tool does not include encryption (SSL/TLS) or authentication mechanisms.
  - Command execution functionality is high-risk; use only in trusted intranet environments or test environments.
  - Do not expose this service on the public internet.

FAQ

1. Windows Compilation Error:
   Ensure ws2_32.lib library is linked. If using MinGW, you may need to link -lws2_32.

2. Connection Refused:
   Check firewall settings to ensure port 8888 is open and the server is running.

3. File Transfer Failure:
   Check path permissions to ensure the server process has permission to read/write to the specified directory.

License

This project is for learning and research purposes only.
