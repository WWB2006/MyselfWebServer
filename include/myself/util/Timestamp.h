#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace myself {

/// 单调时钟时间点：定时器必须用它，避免系统时间被调整导致定时错乱。
using Timestamp = std::chrono::steady_clock::time_point;

inline Timestamp now() {
    return std::chrono::steady_clock::now();
}

/// 把秒数转成时钟时长，供 runAfter/runEvery 使用。
inline std::chrono::steady_clock::duration secondsToDuration(double seconds) {
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(seconds));
}

/// 距离 when 还有多少毫秒；已经过期返回 0。
inline int64_t msUntil(Timestamp when) {
    const Timestamp current = now();
    if (when <= current) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(when - current).count();
}

/// 相对进程启动的毫秒数，只用于日志，避免打印不可读的 time_point。
inline int64_t elapsedMs(Timestamp when) {
    static const Timestamp kStart = now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(when - kStart).count();
}

inline std::string toString(Timestamp when) {
    return std::to_string(elapsedMs(when)) + "ms";
}

}  // namespace myself
