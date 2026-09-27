#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace myself {

/// 运行指标：计数器 + 固定桶直方图。
/// 所有更新都是原子的，可从任意 IO / worker 线程调用；
/// 快照在调用线程内拼装，返回的是某一时刻的近似一致视图。
class Metrics {
public:
    /// 延迟桶上界（毫秒），最后一档固定为 +Inf
    static constexpr size_t kFiniteBuckets = 12;
    static constexpr size_t kBucketCount = kFiniteBuckets + 1;
    static const double kBoundsMs[kFiniteBuckets];

    enum MethodKind {
        kMethodGet = 0,
        kMethodHead,
        kMethodPost,
        kMethodOther,
        kMethodCount,
    };

    /// 某一时刻的指标快照
    struct Snapshot {
        uint64_t connectionsTotal{0};
        int64_t connectionsCurrent{0};
        uint64_t connectionsMax{0};
        uint64_t requestsTotal{0};
        uint64_t errorsTotal{0};
        uint64_t bytesReadTotal{0};
        uint64_t bytesWrittenTotal{0};
        uint64_t statusClasses[5]{0, 0, 0, 0, 0};   // 1xx 到 5xx
        uint64_t methodCounts[kMethodCount]{0, 0, 0, 0};
        uint64_t latencyCount{0};
        double latencySumMs{0.0};
        double latencyP50Ms{0.0};
        double latencyP95Ms{0.0};
        double latencyP99Ms{0.0};
        uint64_t latencyBuckets[kBucketCount]{0};
        uint64_t uptimeSeconds{0};
    };

    Metrics();

    void onConnectionOpened();
    void onConnectionClosed();

    void onRequestStarted(MethodKind kind);
    /// statusCode 用于归类到 1xx..5xx；latencyMs 进入直方图
    void onRequestFinished(int statusCode, double latencyMs);

    void onError();
    void onBytesRead(size_t bytes);
    void onBytesWritten(size_t bytes);

    Snapshot snapshot() const;

    /// Prometheus 文本格式；logLines 由调用方传入，避免 util 依赖 log 模块
    std::string renderPrometheus(uint64_t logLines) const;

    /// 单元测试或压测前重置
    void reset();

    /// 全局注册表：net / timer 等模块通过 metrics::* 便捷函数上报，
    /// 未安装实例时所有便捷函数都是空操作，便于单元测试。
    static void install(Metrics* metrics);
    static Metrics* global();

private:
    static double quantileFromBuckets(const uint64_t* buckets, uint64_t total, double q);

    std::atomic<uint64_t> connectionsTotal_{0};
    std::atomic<int64_t> connectionsCurrent_{0};
    std::atomic<uint64_t> connectionsMax_{0};
    std::atomic<uint64_t> requestsTotal_{0};
    std::atomic<uint64_t> errorsTotal_{0};
    std::atomic<uint64_t> bytesReadTotal_{0};
    std::atomic<uint64_t> bytesWrittenTotal_{0};
    std::atomic<uint64_t> statusClasses_[5]{};
    std::atomic<uint64_t> methodCounts_[kMethodCount]{};
    std::atomic<uint64_t> latencyBuckets_[kBucketCount]{};
    std::atomic<uint64_t> latencyCount_{0};
    std::atomic<uint64_t> latencySumMicros_{0};
    std::chrono::steady_clock::time_point startTime_;
};

/// 便捷上报接口：内部读写全局实例，未安装时不做任何事
namespace metrics {

void connectionOpened();
void connectionClosed();
void requestStarted(Metrics::MethodKind kind);
void requestFinished(int statusCode, double latencyMs);
void error();
void bytesRead(size_t bytes);
void bytesWritten(size_t bytes);

}  // namespace metrics

}  // namespace myself
