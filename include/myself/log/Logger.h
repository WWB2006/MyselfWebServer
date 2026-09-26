#pragma once

#include <cstddef>
#include <string>

#include "myself/log/LogStream.h"

namespace myself {

enum LogLevel {
    kTrace = 0,
    kDebug,
    kInfo,
    kWarn,
    kError,
    kFatal,
    kNumLogLevels,
};

const char* logLevelName(LogLevel level);
LogLevel logLevelFromString(const std::string& name, LogLevel fallback = kInfo);

/// 日志前端：负责格式化，实际输出由 OutputFunc 决定（控制台或异步文件）。
class Logger {
public:
    using OutputFunc = void (*)(const char* data, size_t len);
    using FlushFunc = void (*)();

    Logger(const char* file, int line, LogLevel level);
    ~Logger();

    LogStream& stream() { return stream_; }

    /// 全局最低输出级别，低于它的日志会被直接丢弃。
    static void setLevel(LogLevel level);
    static LogLevel level();

    /// 设置输出目标；默认输出到 stdout。
    static void setOutput(OutputFunc output, FlushFunc flush);
    static void flush();

private:
    static const char* baseName(const char* path);

    LogStream stream_;
    LogLevel level_;
    const char* file_;
    int line_;
};

}  // namespace myself

#define LOG_TRACE                                                       \
    if (myself::Logger::level() <= myself::kTrace)                      \
    myself::Logger(__FILE__, __LINE__, myself::kTrace).stream()
#define LOG_DEBUG                                                       \
    if (myself::Logger::level() <= myself::kDebug)                      \
    myself::Logger(__FILE__, __LINE__, myself::kDebug).stream()
#define LOG_INFO                                                        \
    if (myself::Logger::level() <= myself::kInfo)                       \
    myself::Logger(__FILE__, __LINE__, myself::kInfo).stream()
#define LOG_WARN                                                        \
    if (myself::Logger::level() <= myself::kWarn)                       \
    myself::Logger(__FILE__, __LINE__, myself::kWarn).stream()
#define LOG_ERROR                                                       \
    if (myself::Logger::level() <= myself::kError)                      \
    myself::Logger(__FILE__, __LINE__, myself::kError).stream()
#define LOG_FATAL                                                       \
    if (myself::Logger::level() <= myself::kFatal)                      \
    myself::Logger(__FILE__, __LINE__, myself::kFatal).stream()
