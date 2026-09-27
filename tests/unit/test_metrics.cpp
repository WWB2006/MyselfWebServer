#include <string>

#include <gtest/gtest.h>

#include "myself/util/Metrics.h"

namespace {

/// 从 Prometheus 文本里取某一行后缀的值；找不到返回 -1
long long extractValue(const std::string& text, const std::string& prefix) {
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t end = text.find('\n', pos);
        const std::string line = text.substr(pos, end == std::string::npos ? std::string::npos
                                                                          : end - pos);
        if (line.rfind(prefix, 0) == 0) {
            return std::stoll(line.substr(prefix.size()));
        }
        if (end == std::string::npos) {
            break;
        }
        pos = end + 1;
    }
    return -1;
}

}  // namespace

TEST(MetricsTest, TracksConnectionsAndPeak) {
    myself::Metrics metrics;
    metrics.onConnectionOpened();
    metrics.onConnectionOpened();
    metrics.onConnectionOpened();
    metrics.onConnectionClosed();

    const myself::Metrics::Snapshot snapshot = metrics.snapshot();

    EXPECT_EQ(3u, snapshot.connectionsTotal);
    EXPECT_EQ(2, snapshot.connectionsCurrent);
    EXPECT_EQ(3u, snapshot.connectionsMax);
}

TEST(MetricsTest, ClassifiesStatusCodesAndMethods) {
    myself::Metrics metrics;
    metrics.onRequestStarted(myself::Metrics::kMethodGet);
    metrics.onRequestFinished(200, 1.0);
    metrics.onRequestStarted(myself::Metrics::kMethodHead);
    metrics.onRequestFinished(204, 2.0);
    metrics.onRequestStarted(myself::Metrics::kMethodGet);
    metrics.onRequestFinished(404, 3.0);
    metrics.onRequestFinished(500, 4.0);

    const myself::Metrics::Snapshot snapshot = metrics.snapshot();

    EXPECT_EQ(4u, snapshot.requestsTotal);
    EXPECT_EQ(2u, snapshot.statusClasses[1]);  // 2xx
    EXPECT_EQ(1u, snapshot.statusClasses[3]);  // 4xx
    EXPECT_EQ(1u, snapshot.statusClasses[4]);  // 5xx
    EXPECT_EQ(2u, snapshot.methodCounts[myself::Metrics::kMethodGet]);
    EXPECT_EQ(1u, snapshot.methodCounts[myself::Metrics::kMethodHead]);
}

TEST(MetricsTest, CountsErrorsAndBytes) {
    myself::Metrics metrics;
    metrics.onError();
    metrics.onError();
    metrics.onBytesRead(100);
    metrics.onBytesWritten(250);

    const myself::Metrics::Snapshot snapshot = metrics.snapshot();

    EXPECT_EQ(2u, snapshot.errorsTotal);
    EXPECT_EQ(100u, snapshot.bytesReadTotal);
    EXPECT_EQ(250u, snapshot.bytesWrittenTotal);
}

TEST(MetricsTest, HistogramBucketsUseAccumulatedSemanticsInPrometheusOutput) {
    myself::Metrics metrics;
    metrics.onRequestFinished(200, 0.5);   // <=1ms
    metrics.onRequestFinished(200, 3.0);   // <=5ms
    metrics.onRequestFinished(200, 1500);  // <=2000ms

    const myself::Metrics::Snapshot snapshot = metrics.snapshot();
    EXPECT_EQ(3u, snapshot.latencyCount);

    // 内部按档计数
    EXPECT_EQ(1u, snapshot.latencyBuckets[0]);   // le=1
    EXPECT_EQ(1u, snapshot.latencyBuckets[2]);   // le=5 这一档
    EXPECT_EQ(1u, snapshot.latencyBuckets[10]);  // le=2000 这一档

    // 渲染时必须累加，否则 Prometheus 的 histogram_quantile 会算错
    const std::string text = metrics.renderPrometheus(0);
    EXPECT_EQ(1, extractValue(text, "myself_request_latency_ms_bucket{le=\"1\"} "));
    EXPECT_EQ(2, extractValue(text, "myself_request_latency_ms_bucket{le=\"5\"} "));
    EXPECT_EQ(2, extractValue(text, "myself_request_latency_ms_bucket{le=\"200\"} "));
    EXPECT_EQ(3, extractValue(text, "myself_request_latency_ms_bucket{le=\"+Inf\"} "));
    EXPECT_EQ(3, extractValue(text, "myself_request_latency_ms_count "));
}

TEST(MetricsTest, EstimatesQuantilesFromBuckets) {
    myself::Metrics metrics;
    for (int i = 0; i < 99; ++i) {
        metrics.onRequestFinished(200, 10.0);  // 全部落在 le=10
    }
    metrics.onRequestFinished(200, 5000.0);    // 一个慢请求

    const myself::Metrics::Snapshot snapshot = metrics.snapshot();

    EXPECT_LE(snapshot.latencyP50Ms, 10.0);
    EXPECT_GE(snapshot.latencyP99Ms, 10.0);
    EXPECT_LE(snapshot.latencyP99Ms, 10000.0);
}

TEST(MetricsTest, RendersPrometheusTextWithAllFamilies) {
    myself::Metrics metrics;
    metrics.onConnectionOpened();
    metrics.onRequestFinished(200, 2.0);

    const std::string text = metrics.renderPrometheus(7);

    EXPECT_NE(std::string::npos, text.find("# TYPE myself_requests_total counter"));
    EXPECT_NE(std::string::npos, text.find("# TYPE myself_request_latency_ms histogram"));
    EXPECT_NE(std::string::npos, text.find("myself_requests_by_status{class=\"2xx\"} 1"));
    EXPECT_NE(std::string::npos, text.find("myself_log_lines_total 7"));
    EXPECT_NE(std::string::npos, text.find("myself_uptime_seconds "));
}

TEST(MetricsTest, ResetClearsCounters) {
    myself::Metrics metrics;
    metrics.onConnectionOpened();
    metrics.onRequestFinished(200, 1.0);
    metrics.onError();

    metrics.reset();
    const myself::Metrics::Snapshot snapshot = metrics.snapshot();

    EXPECT_EQ(0u, snapshot.connectionsTotal);
    EXPECT_EQ(0, snapshot.connectionsCurrent);
    EXPECT_EQ(0u, snapshot.requestsTotal);
    EXPECT_EQ(0u, snapshot.errorsTotal);
    EXPECT_EQ(0u, snapshot.latencyCount);
}

TEST(MetricsTest, GlobalConvenienceFunctionsAreSafeWithoutInstall) {
    myself::Metrics::install(nullptr);

    // 未安装实例时必须是空操作，不能崩溃
    myself::metrics::connectionOpened();
    myself::metrics::connectionClosed();
    myself::metrics::requestStarted(myself::Metrics::kMethodGet);
    myself::metrics::requestFinished(200, 1.0);
    myself::metrics::error();
    myself::metrics::bytesRead(10);
    myself::metrics::bytesWritten(10);

    myself::Metrics metrics;
    myself::Metrics::install(&metrics);
    myself::metrics::connectionOpened();
    EXPECT_EQ(1u, metrics.snapshot().connectionsTotal);

    myself::Metrics::install(nullptr);  // 还原，避免影响其他用例
}
