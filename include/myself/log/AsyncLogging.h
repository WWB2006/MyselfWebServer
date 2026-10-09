#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "myself/log/LogFile.h"
#include "myself/log/LogStream.h"

namespace myself {

/// 异步日志后端：前端线程只把格式化好的日志追加到缓冲，
/// 后台线程按固定间隔把缓冲批量写入 LogFile，避免业务线程等磁盘。
class AsyncLogging {
public:
    using Buffer = FixedBuffer<LogStream::kLargeBuffer>;
    using BufferPtr = std::unique_ptr<Buffer>;

    AsyncLogging(std::string baseName, size_t rollSizeBytes,
                 double flushIntervalSeconds = 3.0);
    ~AsyncLogging();

    AsyncLogging(const AsyncLogging&) = delete;
    AsyncLogging& operator=(const AsyncLogging&) = delete;

    void start();
    void stop();

    /// 线程安全：可在任意业务线程调用。
    /// 只把当前缓冲移交给后台线程，真正的落盘由后台线程完成。
    void append(const char* data, size_t len);
    void flush();

private:
    void threadFunc();

    std::string baseName_;
    size_t rollSize_;
    double flushIntervalSeconds_;
    // 后台线程在锁外读取，必须用原子量，否则是数据竞争
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    BufferPtr current_;
    BufferPtr next_;
    std::vector<BufferPtr> buffers_;
    LogFile logFile_;
};

}  // namespace myself
