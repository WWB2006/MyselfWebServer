#include "myself/net/Epoller.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "myself/net/Channel.h"

namespace myself {

Epoller::Epoller(int maxEvents) : events_(static_cast<size_t>(maxEvents)) {
    epollFd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ < 0) {
        throw std::runtime_error(std::string("epoll_create1: ") + std::strerror(errno));
    }
}

Epoller::~Epoller() {
    if (epollFd_ >= 0) {
        ::close(epollFd_);
        epollFd_ = -1;
    }
}

bool Epoller::add(Channel* channel) {
    epoll_event event{};
    event.data.fd = channel->fd();
    event.events = channel->events();
    if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, channel->fd(), &event) < 0) {
        std::cerr << "[epoller] add fd=" << channel->fd()
                  << " failed: " << std::strerror(errno) << "\n";
        return false;
    }
    return true;
}

bool Epoller::modify(Channel* channel) {
    epoll_event event{};
    event.data.fd = channel->fd();
    event.events = channel->events();
    if (::epoll_ctl(epollFd_, EPOLL_CTL_MOD, channel->fd(), &event) < 0) {
        std::cerr << "[epoller] modify fd=" << channel->fd()
                  << " failed: " << std::strerror(errno) << "\n";
        return false;
    }
    return true;
}

bool Epoller::remove(Channel* channel) {
    if (::epoll_ctl(epollFd_, EPOLL_CTL_DEL, channel->fd(), nullptr) < 0) {
        std::cerr << "[epoller] remove fd=" << channel->fd()
                  << " failed: " << std::strerror(errno) << "\n";
        return false;
    }
    return true;
}

int Epoller::wait(int timeoutMs) {
    const int count = ::epoll_wait(epollFd_, events_.data(),
                                   static_cast<int>(events_.size()), timeoutMs);
    if (count < 0 && errno != EINTR) {
        std::cerr << "[epoller] wait failed: " << std::strerror(errno) << "\n";
    }
    return count;
}

}  // namespace myself

