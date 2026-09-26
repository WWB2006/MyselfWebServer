#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>

#include "myself/timer/Timer.h"
#include "myself/util/Timestamp.h"

namespace myself {

class EventLoop;

/// 定时器队列。
/// 只在所属 EventLoop 的线程里访问：对外的 addTimer / cancel 会通过 runInLoop 转发，
/// 因此内部不需要加锁。事件循环每轮用 nextTimeoutMs() 决定 epoll_wait 的超时时间。
class TimerQueue {
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();

    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;

    /// 线程安全，可在任意线程调用。
    TimerId addTimer(TimerCallback callback, Timestamp when, double intervalSeconds);

    /// 线程安全；取消不存在的定时器是安全的空操作。
    void cancel(TimerId timerId);

    /// 距离最近到期定时器的毫秒数；没有定时器返回 -1（表示可以一直阻塞）。
    int nextTimeoutMs() const;

    /// 执行所有已到期的定时器；由事件循环在每轮结束时调用。
    void handleExpiredTimers(Timestamp currentTime);

    size_t size() const { return entries_.size(); }

private:
    struct Entry {
        Timestamp when;
        uint64_t sequence;
        std::shared_ptr<Timer> timer;
    };

    struct EntryCompare {
        bool operator()(const Entry& lhs, const Entry& rhs) const {
            if (lhs.when != rhs.when) {
                return lhs.when < rhs.when;
            }
            return lhs.sequence < rhs.sequence;  // 同刻到期按序号保持稳定顺序
        }
    };

    using EntrySet = std::set<Entry, EntryCompare>;

    void insertInLoop(const std::shared_ptr<Timer>& timer);
    void cancelInLoop(TimerId timerId);

    EventLoop* loop_;
    EntrySet entries_;
    std::map<uint64_t, std::shared_ptr<Timer>> activeTimers_;
};

}  // namespace myself
