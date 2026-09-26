#pragma once

#include <cstdint>
#include <functional>

#include "myself/util/Timestamp.h"

namespace myself {

using TimerCallback = std::function<void()>;

/// 定时器句柄：取消定时器时用它定位。
/// owner 指向创建它的 TimerQueue，避免拿错队列去取消。
class TimerId {
public:
    TimerId() = default;
    TimerId(uint64_t sequence, const void* owner) : sequence_(sequence), owner_(owner) {}

    uint64_t sequence() const { return sequence_; }
    const void* owner() const { return owner_; }
    bool valid() const { return sequence_ != 0 && owner_ != nullptr; }

    bool operator==(const TimerId& other) const {
        return sequence_ == other.sequence_ && owner_ == other.owner_;
    }
    bool operator!=(const TimerId& other) const { return !(*this == other); }

private:
    uint64_t sequence_{0};
    const void* owner_{nullptr};
};

/// 一个定时任务。intervalSeconds > 0 表示重复定时器。
class Timer {
public:
    Timer(TimerCallback callback, Timestamp when, double intervalSeconds);

    void run() const { callback_(); }

    Timestamp expiration() const { return expiration_; }
    bool repeat() const { return intervalSeconds_ > 0; }
    double interval() const { return intervalSeconds_; }
    uint64_t sequence() const { return sequence_; }

    /// 重复定时器到期后重新计算下一次到期时间。
    void restart(Timestamp currentTime);

private:
    const TimerCallback callback_;
    Timestamp expiration_;
    const double intervalSeconds_;
    const uint64_t sequence_;
};

}  // namespace myself
