#include "myself/log/LogFile.h"

#include <unistd.h>

#include <cstring>
#include <ctime>
#include <utility>

namespace myself {

namespace {

/// 每天至少换一个文件，便于按天归档
constexpr time_t kRollPerSeconds = 24 * 60 * 60;

}  // namespace

LogFile::LogFile(std::string baseName, size_t rollSizeBytes, double flushIntervalSeconds,
                 int checkEveryN)
    : baseName_(std::move(baseName)),
      rollSize_(rollSizeBytes),
      flushIntervalSeconds_(flushIntervalSeconds),
      checkEveryN_(checkEveryN) {
    rollFile();
}

LogFile::~LogFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
}

std::string LogFile::makeFileName(const std::string& baseName, time_t now) {
    struct tm tmValue {};
    ::localtime_r(&now, &tmValue);
    char timeText[32];
    std::strftime(timeText, sizeof(timeText), "%Y%m%d-%H%M%S", &tmValue);

    return baseName + "." + timeText + "." + std::to_string(::getpid()) + ".log";
}

bool LogFile::rollFileLocked() {
    const time_t now = std::time(nullptr);
    const std::string fileName = makeFileName(baseName_, now);

    if (file_ != nullptr) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }

    file_ = std::fopen(fileName.c_str(), "ae");  // append + close-on-exec
    if (file_ == nullptr) {
        return false;
    }
    std::setvbuf(file_, nullptr, _IOFBF, 64 * 1024);  // 全缓冲，减少 write 次数

    writtenBytes_ = 0;
    count_ = 0;
    lastRoll_ = now;
    lastFlush_ = now;
    return true;
}

bool LogFile::rollFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    return rollFileLocked();
}

void LogFile::append(const char* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (file_ == nullptr && !rollFileLocked()) {
        return;  // 无法打开文件时静默丢弃，避免日志系统把程序拖死
    }

    const size_t written = std::fwrite(data, 1, len, file_);
    writtenBytes_ += written;
    ++count_;

    if (writtenBytes_ > rollSize_) {
        rollFileLocked();  // 超过大小立刻换文件
        return;
    }

    if (count_ >= checkEveryN_) {
        count_ = 0;
        const time_t now = std::time(nullptr);
        if (now - lastFlush_ >= static_cast<time_t>(flushIntervalSeconds_)) {
            std::fflush(file_);
            lastFlush_ = now;
        }
        if (now - lastRoll_ >= kRollPerSeconds) {
            rollFileLocked();
        }
    }
}

void LogFile::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fflush(file_);
    }
}

}  // namespace myself
