#include "myself/util/Metrics.h"

#include <algorithm>
#include <atomic>
#include <iomanip>
#include <sstream>

namespace myself {

const double Metrics::kBoundsMs[kFiniteBuckets] = {1,  2,   5,   10,  20,   50,
                                                   100, 200, 500, 1000, 2000, 5000};

namespace {

/// 全局实例指针：只在启动时安装一次，之后只读
std::atomic<Metrics*> g_metrics{nullptr};

std::string formatDouble(double value) {
    std::ostringstream out;
    out << std::setprecision(6) << value;
    return out.str();
}

}  // namespace

Metrics::Metrics() : startTime_(std::chrono::steady_clock::now()) { reset(); }

void Metrics::onConnectionOpened() {
    const uint64_t total = connectionsTotal_.fetch_add(1) + 1;
    const int64_t current = connectionsCurrent_.fetch_add(1) + 1;

    uint64_t previousMax = connectionsMax_.load();
    while (static_cast<uint64_t>(current) > previousMax &&
           !connectionsMax_.compare_exchange_weak(previousMax,
                                                   static_cast<uint64_t>(current))) {
        // compare_exchange_weak 失败时会刷新 previousMax，循环重试即可
    }
    (void)total;
}

void Metrics::onConnectionClosed() {
    connectionsCurrent_.fetch_sub(1);
}

void Metrics::onRequestStarted(MethodKind kind) {
    if (kind >= 0 && kind < kMethodCount) {
        methodCounts_[kind].fetch_add(1);
    }
}

void Metrics::onRequestFinished(int statusCode, double latencyMs) {
    requestsTotal_.fetch_add(1);

    const int classIndex = statusCode / 100 - 1;  // 2xx -> 1
    if (classIndex >= 0 && classIndex < 5) {
        statusClasses_[classIndex].fetch_add(1);
    }

    if (latencyMs < 0.0) {
        latencyMs = 0.0;
    }
    latencyCount_.fetch_add(1);
    latencySumMicros_.fetch_add(static_cast<uint64_t>(latencyMs * 1000.0));

    for (size_t i = 0; i < kFiniteBuckets; ++i) {
        if (latencyMs <= kBoundsMs[i]) {
            latencyBuckets_[i].fetch_add(1);
            return;
        }
    }
    latencyBuckets_[kFiniteBuckets].fetch_add(1);  // +Inf 档
}

void Metrics::onError() {
    errorsTotal_.fetch_add(1);
}

void Metrics::onBytesRead(size_t bytes) {
    bytesReadTotal_.fetch_add(bytes);
}

void Metrics::onBytesWritten(size_t bytes) {
    bytesWrittenTotal_.fetch_add(bytes);
}

void Metrics::reset() {
    connectionsTotal_.store(0);
    connectionsCurrent_.store(0);
    connectionsMax_.store(0);
    requestsTotal_.store(0);
    errorsTotal_.store(0);
    bytesReadTotal_.store(0);
    bytesWrittenTotal_.store(0);
    for (auto& counter : statusClasses_) {
        counter.store(0);
    }
    for (auto& counter : methodCounts_) {
        counter.store(0);
    }
    for (auto& counter : latencyBuckets_) {
        counter.store(0);
    }
    latencyCount_.store(0);
    latencySumMicros_.store(0);
    startTime_ = std::chrono::steady_clock::now();
}

double Metrics::quantileFromBuckets(const uint64_t* buckets, uint64_t total, double q) {
    if (total == 0 || q <= 0.0) {
        return 0.0;
    }
    if (q > 1.0) {
        q = 1.0;
    }

    const double target = q * static_cast<double>(total);
    uint64_t cumulative = 0;

    for (size_t i = 0; i < kBucketCount; ++i) {
        const uint64_t before = cumulative;
        cumulative += buckets[i];
        if (static_cast<double>(cumulative) >= target) {
            const double lower = (i == 0) ? 0.0 : kBoundsMs[i - 1];
            const double upper =
                (i < kFiniteBuckets) ? kBoundsMs[i] : kBoundsMs[kFiniteBuckets - 1] * 2.0;
            if (buckets[i] == 0) {
                return upper;
            }
            const double ratio =
                (target - static_cast<double>(before)) / static_cast<double>(buckets[i]);
            return lower + (upper - lower) * std::min(1.0, std::max(0.0, ratio));
        }
    }
    return kBoundsMs[kFiniteBuckets - 1];
}

Metrics::Snapshot Metrics::snapshot() const {
    Snapshot result;
    result.connectionsTotal = connectionsTotal_.load();
    result.connectionsCurrent = connectionsCurrent_.load();
    result.connectionsMax = connectionsMax_.load();
    result.requestsTotal = requestsTotal_.load();
    result.errorsTotal = errorsTotal_.load();
    result.bytesReadTotal = bytesReadTotal_.load();
    result.bytesWrittenTotal = bytesWrittenTotal_.load();

    for (size_t i = 0; i < 5; ++i) {
        result.statusClasses[i] = statusClasses_[i].load();
    }
    for (size_t i = 0; i < kMethodCount; ++i) {
        result.methodCounts[i] = methodCounts_[i].load();
    }
    for (size_t i = 0; i < kBucketCount; ++i) {
        result.latencyBuckets[i] = latencyBuckets_[i].load();
    }

    result.latencyCount = latencyCount_.load();
    result.latencySumMs = static_cast<double>(latencySumMicros_.load()) / 1000.0;
    result.latencyP50Ms =
        quantileFromBuckets(result.latencyBuckets, result.latencyCount, 0.50);
    result.latencyP95Ms =
        quantileFromBuckets(result.latencyBuckets, result.latencyCount, 0.95);
    result.latencyP99Ms =
        quantileFromBuckets(result.latencyBuckets, result.latencyCount, 0.99);

    const auto elapsed = std::chrono::steady_clock::now() - startTime_;
    result.uptimeSeconds = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
    return result;
}

std::string Metrics::renderPrometheus(uint64_t logLines) const {
    const Snapshot data = snapshot();
    std::ostringstream out;

    out << "# HELP myself_connections_total 累计接受的连接数\n"
        << "# TYPE myself_connections_total counter\n"
        << "myself_connections_total " << data.connectionsTotal << "\n"
        << "# HELP myself_connections_current 当前在线连接数\n"
        << "# TYPE myself_connections_current gauge\n"
        << "myself_connections_current " << data.connectionsCurrent << "\n"
        << "# HELP myself_connections_max 在线连接数历史峰值\n"
        << "# TYPE myself_connections_max gauge\n"
        << "myself_connections_max " << data.connectionsMax << "\n";

    out << "# HELP myself_requests_total 累计处理的 HTTP 请求数\n"
        << "# TYPE myself_requests_total counter\n"
        << "myself_requests_total " << data.requestsTotal << "\n";

    static const char* kClassNames[5] = {"1xx", "2xx", "3xx", "4xx", "5xx"};
    out << "# HELP myself_requests_by_status 按状态码类别统计的请求数\n"
        << "# TYPE myself_requests_by_status counter\n";
    for (size_t i = 0; i < 5; ++i) {
        out << "myself_requests_by_status{class=\"" << kClassNames[i]
            << "\"} " << data.statusClasses[i] << "\n";
    }

    static const char* kMethodNames[Metrics::kMethodCount] = {"GET", "HEAD", "POST", "OTHER"};
    out << "# HELP myself_requests_by_method 按 HTTP 方法统计的请求数\n"
        << "# TYPE myself_requests_by_method counter\n";
    for (size_t i = 0; i < Metrics::kMethodCount; ++i) {
        out << "myself_requests_by_method{method=\"" << kMethodNames[i]
            << "\"} " << data.methodCounts[i] << "\n";
    }

    out << "# HELP myself_errors_total 累计错误数（socket 错误与非法请求）\n"
        << "# TYPE myself_errors_total counter\n"
        << "myself_errors_total " << data.errorsTotal << "\n"
        << "# HELP myself_bytes_read_total 累计读取字节数\n"
        << "# TYPE myself_bytes_read_total counter\n"
        << "myself_bytes_read_total " << data.bytesReadTotal << "\n"
        << "# HELP myself_bytes_written_total 累计写入字节数\n"
        << "# TYPE myself_bytes_written_total counter\n"
        << "myself_bytes_written_total " << data.bytesWrittenTotal << "\n"
        << "# HELP myself_log_lines_total 累计日志行数\n"
        << "# TYPE myself_log_lines_total counter\n"
        << "myself_log_lines_total " << logLines << "\n"
        << "# HELP myself_uptime_seconds 进程运行时长\n"
        << "# TYPE myself_uptime_seconds gauge\n"
        << "myself_uptime_seconds " << data.uptimeSeconds << "\n";

    // Prometheus histogram：le="X" 表示延迟 <= X 的累计数量
    out << "# HELP myself_request_latency_ms 请求处理延迟直方图（毫秒）\n"
        << "# TYPE myself_request_latency_ms histogram\n";
    uint64_t cumulative = 0;
    for (size_t i = 0; i < kFiniteBuckets; ++i) {
        cumulative += data.latencyBuckets[i];
        out << "myself_request_latency_ms_bucket{le=\"" << kBoundsMs[i] << "\"} "
            << cumulative << "\n";
    }
    cumulative += data.latencyBuckets[kFiniteBuckets];
    out << "myself_request_latency_ms_bucket{le=\"+Inf\"} " << cumulative << "\n"
        << "myself_request_latency_ms_sum " << formatDouble(data.latencySumMs) << "\n"
        << "myself_request_latency_ms_count " << data.latencyCount << "\n";

    out << "# HELP myself_request_latency_ms_quantile 由固定桶插值估算的分位延迟\n"
        << "# TYPE myself_request_latency_ms_quantile gauge\n"
        << "myself_request_latency_ms_quantile{quantile=\"0.5\"} "
        << formatDouble(data.latencyP50Ms) << "\n"
        << "myself_request_latency_ms_quantile{quantile=\"0.95\"} "
        << formatDouble(data.latencyP95Ms) << "\n"
        << "myself_request_latency_ms_quantile{quantile=\"0.99\"} "
        << formatDouble(data.latencyP99Ms) << "\n";

    return out.str();
}

void Metrics::install(Metrics* metrics) {
    g_metrics.store(metrics);
}

Metrics* Metrics::global() {
    return g_metrics.load();
}

namespace metrics {

void connectionOpened() {
    if (Metrics* m = Metrics::global()) {
        m->onConnectionOpened();
    }
}

void connectionClosed() {
    if (Metrics* m = Metrics::global()) {
        m->onConnectionClosed();
    }
}

void requestStarted(Metrics::MethodKind kind) {
    if (Metrics* m = Metrics::global()) {
        m->onRequestStarted(kind);
    }
}

void requestFinished(int statusCode, double latencyMs) {
    if (Metrics* m = Metrics::global()) {
        m->onRequestFinished(statusCode, latencyMs);
    }
}

void error() {
    if (Metrics* m = Metrics::global()) {
        m->onError();
    }
}

void bytesRead(size_t bytes) {
    if (Metrics* m = Metrics::global()) {
        m->onBytesRead(bytes);
    }
}

void bytesWritten(size_t bytes) {
    if (Metrics* m = Metrics::global()) {
        m->onBytesWritten(bytes);
    }
}

}  // namespace metrics

}  // namespace myself
