#include "myself/net/Channel.h"

#include <utility>

#include "myself/net/EventLoop.h"

namespace myself {

Channel::Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd) {}

Channel::~Channel() {
    remove();
}

void Channel::update() {
    if (!removed_) {
        loop_->updateChannel(this);
    }
}

void Channel::remove() {
    if (!removed_) {
        removed_ = true;
        loop_->removeChannel(this);
    }
}

void Channel::handleEvent() {
    // 回调可能销毁宿主对象（例如连接关闭），因此先锁住生命周期
    std::shared_ptr<void> guard;
    if (tied_) {
        guard = tie_.lock();
        if (!guard) {
            return;
        }
    }

    if (revents_ & EPOLLHUP) {
        if (closeCallback_) {
            closeCallback_();
        }
        return;
    }
    if (revents_ & (EPOLLERR | EPOLLRDHUP)) {
        if (errorCallback_) {
            errorCallback_();
        }
        return;
    }
    if (revents_ & (kReadEvent)) {
        if (readCallback_) {
            readCallback_();
        }
    }
    if (revents_ & kWriteEvent) {
        if (writeCallback_) {
            writeCallback_();
        }
    }
}

}  // namespace myself

