#include "myself/net/Socket.h"
#include <iostream>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace myself {
namespace {

std::runtime_error sysError(const std::string& what) {
    return std::runtime_error(what + ": " + std::strerror(errno));
}

}  // namespace

Socket::~Socket() {
    std::cerr << "[~Socket] fd=" << fd_ << "\n";   // ← 加这行
    close();
}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void Socket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

int Socket::release() {
    const int fd = fd_;
    fd_ = -1;
    return fd;
}

void Socket::setReuseAddr(bool on) {
    const int value = on ? 1 : 0;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
}

void Socket::setNonBlocking(bool on) {
    const int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
        throw sysError("fcntl(F_GETFL)");
    }
    const int newFlags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(fd_, F_SETFL, newFlags) < 0) {
        throw sysError("fcntl(F_SETFL)");
    }
}

Socket Socket::listenOn(const std::string& ip, int port, int backlog) {
    Socket sock(::socket(AF_INET, SOCK_STREAM, 0));
    if (!sock.valid()) {
        throw sysError("socket");
    }

    // 复用地址，避免重启时因为 TIME_WAIT 报 Address already in use
    sock.setReuseAddr(true);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("invalid ip address: " + ip);
    }

    if (::bind(sock.fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw sysError("bind");
    }
    if (::listen(sock.fd(), backlog) < 0) {
        throw sysError("listen");
    }
    sock.setNonBlocking(true);   // ← 加这行
    return sock;
}

}  // namespace myself

