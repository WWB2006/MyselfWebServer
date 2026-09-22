#include "myself/net/EventLoop.h"

#include <iostream>

#include "myself/net/Channel.h"
#include "myself/net/Epoller.h"

namespace myself {

EventLoop::EventLoop() : epoller_(std::make_unique<Epoller>()) {}

EventLoop::~EventLoop() = default;

void EventLoop::loop(int timeoutMs) {
    quit_ = false;
    while (!quit_) {
        const int count = epoller_->wait(timeoutMs);
        if (count < 0) {
            continue;  // 被信号打断，继续等待
        }
        dispatch(epoller_->events(), count);
    }
}

void EventLoop::dispatch(const std::vector<epoll_event>& events, int count) {
    for (int i = 0; i < count; ++i) {
        const int fd = events[static_cast<size_t>(i)].data.fd;
        auto it = channels_.find(fd);
        if (it == channels_.end()) {
            // 该 fd 已在本轮事件处理中被移除，跳过
            continue;
        }
        Channel* channel = it->second;
        channel->setRevents(events[static_cast<size_t>(i)].events);
        channel->handleEvent();
    }
}

void EventLoop::updateChannel(Channel* channel) {
    const int fd = channel->fd();
    auto it = channels_.find(fd);
    if (it == channels_.end()) {
        if (channel->isNoneEvent()) {
            return;
        }
        channels_[fd] = channel;
        epoller_->add(channel);
        ++channelCount_;
        return;
    }
    if (channel->isNoneEvent()) {
        removeChannel(channel);
        return;
    }
    epoller_->modify(channel);
}

void EventLoop::removeChannel(Channel* channel) {
    const int fd = channel->fd();
    auto it = channels_.find(fd);
    if (it == channels_.end()) {
        return;
    }
    epoller_->remove(channel);
    channels_.erase(it);
    --channelCount_;
}

}  // namespace myself

