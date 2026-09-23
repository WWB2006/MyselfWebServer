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
        std::cerr << "[loop] waiting...\n";   // ← 加这行，看有没有在循环
        const int count = epoller_->wait(timeoutMs);
        if (count < 0) {
            continue;  // 被信号打断，继续等待
        }
        if(count>0){
            std::cerr << "[loop] epoll_wait returned " << count << "\n";   // ← 加这行
        }
        dispatch(epoller_->events(), count);
    }
}

void EventLoop::dispatch(const std::vector<epoll_event>& events, int count) {
    if (count > 0) {
        std::cerr << "[dispatch] count=" << count
                  << " fd=" << events[0].data.fd << "\n";   // ← 加这行
    }
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
            std::cerr << "[updateChannel] fd=" << fd << " isNoneEvent, skip\n";
            return;
        }
        channels_[fd] = channel;
        std::cerr << "[updateChannel] ADD fd=" << fd << " events=" << channel->events() << "\n";
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

