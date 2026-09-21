#pragma once

#include <string>

namespace myself {

/// RAII 封装一个文件描述符：析构自动 close，禁止拷贝，允许移动。
class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    int fd() const { return fd_; }
    bool valid() const { return fd_ >= 0; }

    /// 主动关闭并释放描述符。
    void close();

    /// 交出所有权，返回值由调用方负责关闭。
    int release();

    /// 创建监听套接字：socket + SO_REUSEADDR + bind + listen。
    /// ip 传 "0.0.0.0" 表示监听所有网卡。
    static Socket listenOn(const std::string& ip, int port, int backlog = 128);

    void setReuseAddr(bool on);
    void setNonBlocking(bool on);

private:
    int fd_{-1};
};

}  // namespace myself

