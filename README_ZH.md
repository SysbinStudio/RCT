Remote Control Tool

一个基于 C++ 开发的跨平台远程管理工具，支持 Windows 和 Linux/macOS 系统。该工具允许用户通过命令行界面执行远程命令、上传和下载文件。

功能特性

- 跨平台支持：兼容 Windows (Winsock) 和 Unix-like 系统 (POSIX sockets)。
- 远程命令执行：在服务端或客户端上执行系统命令并返回结果。
- 文件传输：
  - sendfile：从本地发送文件到远程主机。
  - recvfile：从远程主机接收文件到本地。
- 二进制安全传输：使用长度前缀协议确保数据完整接收。
- 多线程处理：服务端使用独立线程处理客户端连接，保持主控制循环响应。

编译要求

- C++11 或更高版本编译器 (GCC, Clang, MSVC)
- CMake 或 Makefile (可选，也可直接使用编译器命令)

Linux/macOS 编译

g++ -std=c++11 -o rct main.cpp -pthread

Windows 编译 (MSVC)

cl /EHsc main.cpp ws2_32.lib

使用方法

启动服务端

在目标机器上运行以下命令以启动监听服务（默认端口 8888）：

./rct --server

启动客户端

在控制端机器上运行以下命令连接到服务端：

./rct --client <服务器IP地址>

例如：

./rct --client 192.168.1.100

查看版本

./rct --version

控制命令

连接成功后，在客户端的控制台输入以下命令进行操作：

命令 | 说明 | 示例
--- | --- | ---
cmd | 执行远程系统命令 | cmd ls -la 或 cmd dir
sendfile | 上传文件到远程主机 | sendfile local.txt remote/path/file.txt
recvfile | 从远程主机下载文件 | recvfile remote/path/file.txt local.txt
exit / quit | 断开连接并退出程序 | exit

命令详解

1. 执行命令 (cmd)

在远程主机上执行指定的 Shell 或 CMD 命令，并将输出打印到本地控制台。

remote> cmd whoami

2. 发送文件 (sendfile)

将本地文件上传至远程主机的指定路径。

remote> sendfile ./photo.jpg /tmp/photo.jpg

3. 接收文件 (recvfile)

从远程主机下载指定路径的文件到本地。

remote> recvfile /var/log/syslog ./syslog_backup.log

技术细节

- 通信协议：TCP
- 默认端口：8888
- 数据编码：
  - 字符串传输采用 uint32_t 网络字节序长度前缀 + 原始内容。
  - 文件大小传输采用 uint64_t 网络字节序。
- 安全性警告：
  - 本工具未包含加密（SSL/TLS）或身份验证机制。
  - 命令执行功能具有高风险，仅在受信任的内网环境或测试环境中使用。
  - 请勿在公共互联网上暴露此服务。

常见问题

1. Windows 下编译错误：
   确保链接了 ws2_32.lib 库。如果使用 MinGW，可能需要链接 -lws2_32。

2. 连接被拒绝：
   检查防火墙设置，确保端口 8888 已开放，且服务端正在运行。

3. 文件传输失败：
   检查路径权限，确保服务端进程有权限读取/写入指定目录。

许可证

本项目仅供学习和研究使用。
