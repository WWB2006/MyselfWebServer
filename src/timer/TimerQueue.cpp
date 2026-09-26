#include "myself/timer/TimerQueue.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "myself/net/EventLoop.h"

namespace myself {

namespace {

/// 单次 epoll_wait 的最长等待时间，避免超时值溢出成负数。
constexpr int64_t kMaxTimeoutMs = 24 * 60 * 60 * 1000;  // 1 天

}  // namespace

TimerQueue::TimerQueue(EventLoop* loop) : loop_(loop) {}

TimerQueue::~TimerQueue() = default;

TimerId TimerQueue::addTimer(TimerCallback callback, Timestamp when,
                             double intervalSeconds) {
    auto timer = std::make_shared<Timer>(std::move(callback), when, intervalSeconds);
    const TimerId id(timer->sequence(), this);

    // 不在本线程时自动转回 loop 线程执行，顺便唤醒阻塞中的 epoll_wait
    loop_->runInLoop([this, timer] { insertInLoop(timer); });
    return id;
}

void TimerQueue::cancel(TimerId timerId) {
    if (!timerId.valid()) {
        return;
    }
    loop_->runInLoop([this, timerId] { cancelInLoop(timerId); });
}

void TimerQueue::insertInLoop(const std::shared_ptr<Timer>& timer) {
    activeTimers_[timer->sequence()] = timer;
    entries_.insert(Entry{timer->expiration(), timer->sequence(), timer});
}

void TimerQueue::cancelInLoop(TimerId timerId) {
    if (timerId.owner() != this) {
        return;  // 拿别的队列的句柄来取消，直接忽略
    }
    const auto it = activeTimers_.find(timerId.sequence());
    if (it == activeTimers_.end()) {
        return;  // 已经触发过或已被取消
    }

    const std::shared_ptr<Timer> timer = it->second;
    activeTimers_.erase(it);
    entries_.erase(Entry{timer->expiration(), timer->sequence(), nullptr});
}

int TimerQueue::nextTimeoutMs() const {
    if (entries_.empty()) {
        return -1;  // 没有定时器：epoll_wait 可以一直阻塞
    }
    const int64_t ms = msUntil(entries_.begin()->when);
    if (ms <= 0) {
        return 0;  // 已经到期，立刻返回处理
    }
    return static_cast<int>(std::min<int64_t>(ms, kMaxTimeoutMs));
}

void TimerQueue::handleExpiredTimers(Timestamp currentTime) {
    std::vector<std::shared_ptr<Timer>> expired;
    while (!entries_.empty() && entries_.begin()->when <= currentTime) {
        expired.push_back(entries_.begin()->timer);
        entries_.erase(entries_.begin());
    }

    for (const auto& timer : expired) {
        if (timer->repeat()) {
            // 先重排再执行：这样回调里取消自己也能正确移除
            timer->restart(currentTime);
            insertInLoop(timer);
        } else {
            activeTimers_.erase(timer->sequence());
        }
        timer->run();
    }
}

}  // namespace myself
