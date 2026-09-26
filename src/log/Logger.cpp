#include "myself/log/Logger.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

namespace myself {

namespace {

std::mutex g_outputMutex;
std::atomic<LogLevel> g_level{kInfo};

void defaultOutput(const char* data, size_t len) {
    std::lock_guard<std::mutex> lock(g_outputMutex);
    std::fwrite(data, 1, len, stdout);
}

void defaultFlush() {
    std::lock_guard<std::mutex> lock(g_outputMutex);
    std::fflush(stdout);
}

Logger::OutputFunc g_output = defaultOutput;
Logger::FlushFunc g_flush = defaultFlush;

pid_t currentThreadId() {
    return static_cast<pid_t>(::syscall(SYS_gettid));
}

/// 输出格式：20260926 20:15:03.123456 [INFO ] [tid:12345] message - file.cpp:42
void appendTimestamp(LogStream* stream) {
    struct timespec ts {};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmValue {};
    ::localtime_r(&ts.tv_sec, &tmValue);

    char date[32];
    std::strftime(date, sizeof(date), "%Y%m%d %H:%M:%S", &tmValue);
    stream->operator<<(date) << "." << static_cast<int>(ts.tv_nsec / 1000);
}

}  // namespace

const char* logLevelName(LogLevel level) {
    switch (level) {
        case kTrace:
            return "TRACE";
        case kDebug:
            return "DEBUG";
        case kInfo:
            return "INFO ";
        case kWarn:
            return "WARN ";
        case kError:
            return "ERROR";
        case kFatal:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

LogLevel logLevelFromString(const std::string& name, LogLevel fallback) {
    if (name == "trace") {
        return kTrace;
    }
    if (name == "debug") {
        return kDebug;
    }
    if (name == "info") {
        return kInfo;
    }
    if (name == "warn" || name == "warning") {
        return kWarn;
    }
    if (name == "error") {
        return kError;
    }
    if (name == "fatal") {
        return kFatal;
    }
    return fallback;
}

Logger::Logger(const char* file, int line, LogLevel level)
    : level_(level), file_(file), line_(line) {
    appendTimestamp(&stream_);
    stream_ << " [" << logLevelName(level_) << "] [tid:" << currentThreadId() << "] ";
}

Logger::~Logger() {
    stream_ << " - " << baseName(file_) << ":" << line_ << "\n";
    const LogStream::Buffer& buffer = stream_.buffer();
    g_output(buffer.data(), buffer.length());

    if (level_ == kFatal) {
        g_flush();
        std::abort();
    }
}

const char* Logger::baseName(const char* path) {
    const char* base = std::strrchr(path, '/');
    return base != nullptr ? base + 1 : path;
}

void Logger::setLevel(LogLevel level) {
    g_level.store(level);
}

LogLevel Logger::level() {
    return g_level.load();
}

void Logger::setOutput(OutputFunc output, FlushFunc flush) {
    g_output = output != nullptr ? output : defaultOutput;
    g_flush = flush != nullptr ? flush : defaultFlush;
}

void Logger::flush() {
    g_flush();
}

}  // namespace myself

