#pragma once

#include <cstddef>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace myself {

/// 日志文件：追加写入，按大小与日期轮转。
class LogFile {
public:
    LogFile(std::string baseName, size_t rollSizeBytes, double flushIntervalSeconds = 3.0,
            int checkEveryN = 1024);
    ~LogFile();

    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    void append(const char* data, size_t len);
    void flush();
    bool rollFile();

    /// 文件名形如 server.20260926-201503.12345.log
    static std::string makeFileName(const std::string& baseName, time_t now);

private:
    bool rollFileLocked();

    const std::string baseName_;
    const size_t rollSize_;
    const double flushIntervalSeconds_;
    const int checkEveryN_;
    int count_{0};
    size_t writtenBytes_{0};
    time_t lastFlush_{0};
    time_t lastRoll_{0};
    std::FILE* file_{nullptr};
    std::mutex mutex_;
};

}  // namespace myself
