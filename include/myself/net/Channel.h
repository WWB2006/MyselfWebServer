#pragma once

#include <sys/epoll.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace myself {

class EventLoop;

/// 一个 fd 及其关心的事件与回调。
/// 事件变化时通过 EventLoop 同步到 epoll；回调执行期间用 tie_ 保住宿主对象。
class Channel {
public:
    using EventCallback = std::function<void()>;

    static constexpr uint32_t kReadEvent = EPOLLIN | EPOLLPRI;
    static constexpr uint32_t kWriteEvent = EPOLLOUT;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    int fd() const { return fd_; }
    uint32_t events() const { return events_; }
    void setRevents(uint32_t revents) { revents_ = revents; }
    bool isWriting() const { return (events_ & kWriteEvent) != 0; }
    bool isNoneEvent() const { return events_ == 0; }

    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    /// 绑定宿主对象的生命周期，避免回调期间宿主被析构。
    void tie(const std::shared_ptr<void>& owner) {
        tie_ = owner;
        tied_ = true;
    }

    void enableReading() {
        events_ |= kReadEvent;
        update();
    }
    void enableWriting() {
        events_ |= kWriteEvent;
        update();
    }
    void disableWriting() {
        events_ &= ~kWriteEvent;
        update();
    }
    void disableAll() {
        events_ = 0;
        update();
    }

    /// 从事件循环中注销，之后不会再收到任何事件。
    void remove();

    void handleEvent();

private:
    void update();

    EventLoop* loop_;
    const int fd_;
    uint32_t events_{0};
    uint32_t revents_{0};
    bool tied_{false};
    bool removed_{false};
    std::weak_ptr<void> tie_;
    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

}  // namespace myself

