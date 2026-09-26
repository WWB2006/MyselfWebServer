#include "myself/timer/Timer.h"

#include <atomic>
#include <utility>

namespace myself {

namespace {

/// 全局自增序号：保证同一时刻到期的定时器有稳定顺序。
uint64_t nextTimerSequence() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}

}  // namespace

Timer::Timer(TimerCallback callback, Timestamp when, double intervalSeconds)
    : callback_(std::move(callback)),
      expiration_(when),
      intervalSeconds_(intervalSeconds),
      sequence_(nextTimerSequence()) {}

void Timer::restart(Timestamp currentTime) {
    if (!repeat()) {
        return;
    }
    // 以当前时间为基准累加，避免回调执行耗时导致的漂移累积
    expiration_ = currentTime + secondsToDuration(intervalSeconds_);
}

}  // namespace myself
