#include "myself/util/TokenBucket.h"

#include <algorithm>

namespace myself {

TokenBucket::TokenBucket(double rate, double burst) {
    configure(rate, burst);
}

void TokenBucket::configure(double rate, double burst) {
    std::lock_guard<std::mutex> lock(mutex_);
    rate_ = std::max(0.0, rate);
    if (burst <= 0.0) {
        burst_ = rate_;
    } else {
        burst_ = burst;
    }
    tokens_ = burst_;
    lastRefillTime_ = std::chrono::steady_clock::now();
}

bool TokenBucket::tryAcquire(double tokens) {
    if (rate_ <= 0.0 || tokens <= 0.0) {
        return true;  // 限流未启用或请求 0 令牌，直接放行
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    refillLocked(now);

    if (tokens_ >= tokens) {
        tokens_ -= tokens;
        return true;
    }
    return false;
}

double TokenBucket::availableTokens() const {
    if (rate_ <= 0.0) {
        return 0.0;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - lastRefillTime_);
    if (elapsed.count() > 0) {
        const double deltaSeconds = static_cast<double>(elapsed.count()) / 1000000.0;
        return std::min(burst_, tokens_ + deltaSeconds * rate_);
    }
    return tokens_;
}

bool TokenBucket::isEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rate_ > 0.0;
}

double TokenBucket::rate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rate_;
}

double TokenBucket::burst() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return burst_;
}

void TokenBucket::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_ = burst_;
    lastRefillTime_ = std::chrono::steady_clock::now();
}

void TokenBucket::refillLocked(std::chrono::steady_clock::time_point now) {
    if (rate_ <= 0.0) {
        return;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - lastRefillTime_);
    if (elapsed.count() <= 0) {
        return;
    }

    const double deltaSeconds = static_cast<double>(elapsed.count()) / 1000000.0;
    tokens_ = std::min(burst_, tokens_ + deltaSeconds * rate_);
    lastRefillTime_ = now;
}

}  // namespace myself
