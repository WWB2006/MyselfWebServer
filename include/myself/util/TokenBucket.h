#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>

namespace myself {

/// 令牌桶限流器 (Token Bucket Rate Limiter)
/// 核心原理：
/// 1. 平滑限制系统处理 HTTP 请求的长期平均速率（rate tokens/sec）
/// 2. 允许预设容量的短时突发流量（burst capacity）
/// 3. 基于高精度单调时钟（std::chrono::steady_clock）的微秒级惰性补发（Lazy Refill），
///    避免了额外后台定时轮询线程的上下文切换与资源开销
/// 4. 线程安全（Mutex-guarded），支持多 IO 事件循环线程高并发调用
class TokenBucket {
public:
    /// @param rate 每秒补充的令牌数（例如 1000 表示限制为 1000 QPS，0 表示不限流）
    /// @param burst 令牌桶最大容量（允许短时突发的峰值，若 <= 0 则默认等于 rate）
    explicit TokenBucket(double rate = 0.0, double burst = 0.0);

    /// 尝试获取指定数量的令牌（默认消费 1 个令牌）
    /// @return true 获取成功（放行请求），false 令牌耗尽（触发限流防护）
    bool tryAcquire(double tokens = 1.0);

    /// 动态重新配置速率与突发容量
    void configure(double rate, double burst = 0.0);

    /// 查询当前可用令牌数量快照
    double availableTokens() const;

    /// 是否启用了限流（rate > 0）
    bool isEnabled() const;

    double rate() const;
    double burst() const;

    /// 重置令牌桶至满容量状态
    void reset();

private:
    void refillLocked(std::chrono::steady_clock::time_point now);

    mutable std::mutex mutex_;
    double rate_{0.0};
    double burst_{0.0};
    double tokens_{0.0};
    std::chrono::steady_clock::time_point lastRefillTime_;
};

}  // namespace myself
