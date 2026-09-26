#include "myself/net/EventLoop.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "myself/net/Channel.h"
#include "myself/net/Epoller.h"
#include "myself/timer/TimerQueue.h"

namespace myself {

EventLoop::EventLoop()
    : epoller_(std::make_unique<Epoller>()), threadId_(std::this_thread::get_id()) {
    timerQueue_ = std::make_unique<TimerQueue>(this);

    // eventfd 作为唤醒通道：其他线程投递任务时写 8 字节，让 epoll_wait 立即返回
    wakeupFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ < 0) {
        throw std::runtime_error("eventfd: failed to create wakeup fd");
    }

    wakeupChannel_ = std::make_unique<Channel>(this, wakeupFd_);
    wakeupChannel_->setReadCallback([this] { handleWakeup(); });
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop() {
    if (wakeupChannel_) {
        wakeupChannel_->disableAll();
        wakeupChannel_->remove();
    }
    if (wakeupFd_ >= 0) {
        ::close(wakeupFd_);
        wakeupFd_ = -1;
    }
}

void EventLoop::loop(int timeoutMs) {
    assertInLoopThread();
    quit_.store(false);

    while (!quit_.load()) {
        // 传入 -1 时按最近到期的定时器自动计算等待时间
        const int waitMs = (timeoutMs >= 0) ? timeoutMs : timerQueue_->nextTimeoutMs();
        const int count = epoller_->wait(waitMs);
        if (count > 0) {
            dispatch(epoller_->events(), count);
        }
        timerQueue_->handleExpiredTimers(now());  // 处理到期定时器
        doPendingFunctors();  // 每轮都处理跨线程投递的任务
    }
}

void EventLoop::quit() {
    quit_.store(true);
    if (!isInLoopThread()) {
        wakeup();  // 其他线程调用时，把阻塞在 epoll_wait 的循环叫醒
    }
}

void EventLoop::runInLoop(Functor cb) {
    if (isInLoopThread()) {
        cb();
    } else {
        queueInLoop(std::move(cb));
    }
}

void EventLoop::queueInLoop(Functor cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctors_.push_back(std::move(cb));
    }
    // 非本线程，或正在执行待办任务（可能又投递了新任务）时需要唤醒
    if (!isInLoopThread() || callingPendingFunctors_.load()) {
        wakeup();
    }
}

void EventLoop::assertInLoopThread() const {
    if (isInLoopThread()) {
        return;
    }
    std::cerr << "[loop:" << (name_.empty() ? "unnamed" : name_)
              << "] fatal: operation must run in its own thread\n";
    std::abort();
}

void EventLoop::wakeup() {
    const uint64_t one = 1;
    const ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
    (void)n;  // eventfd 非阻塞写，失败说明已有待处理唤醒，可以忽略
}

void EventLoop::handleWakeup() {
    uint64_t value = 0;
    while (::read(wakeupFd_, &value, sizeof(value)) > 0) {
        // 读空为止，避免残留计数导致后续空转
    }
}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    callingPendingFunctors_.store(true);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }
    for (const auto& functor : functors) {
        if (functor) {
            functor();
        }
    }
    callingPendingFunctors_.store(false);
}

size_t EventLoop::pendingTaskCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingFunctors_.size();
}

TimerId EventLoop::runAt(Timestamp when, TimerCallback callback) {
    return timerQueue_->addTimer(std::move(callback), when, 0.0);
}

TimerId EventLoop::runAfter(double delaySeconds, TimerCallback callback) {
    return timerQueue_->addTimer(std::move(callback), now() + secondsToDuration(delaySeconds),
                                 0.0);
}

TimerId EventLoop::runEvery(double intervalSeconds, TimerCallback callback) {
    return timerQueue_->addTimer(std::move(callback),
                                 now() + secondsToDuration(intervalSeconds),
                                 intervalSeconds);
}

void EventLoop::cancelTimer(TimerId timerId) {
    timerQueue_->cancel(timerId);
}

size_t EventLoop::timerCount() const {
    return timerQueue_->size();
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
    assertInLoopThread();

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
    assertInLoopThread();

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
