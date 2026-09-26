#pragma once

#include <cstddef>
#include <cstring>
#include <string>

namespace myself {

/// 固定大小缓冲区：日志格式化只在栈上完成，避免每条日志都分配内存。
template <size_t SIZE>
class FixedBuffer {
public:
    FixedBuffer() : cur_(data_) {}

    void append(const char* data, size_t len) {
        if (avail() > len) {
            std::memcpy(cur_, data, len);
            cur_ += len;
            *cur_ = '\0';
        }
    }

    const char* data() const { return data_; }
    size_t length() const { return static_cast<size_t>(cur_ - data_); }
    size_t avail() const { return static_cast<size_t>(end() - cur_ - 1); }
    char* current() { return cur_; }
    void add(size_t len) { cur_ += len; }
    void reset() {
        cur_ = data_;
        *cur_ = '\0';
    }
    void zero() { std::memset(data_, 0, sizeof(data_)); }

    std::string toString() const { return std::string(data_, length()); }

private:
    const char* end() const { return data_ + sizeof(data_); }

    char data_[SIZE];
    char* cur_;
};

/// 支持常见类型的流式输出，用法与 std::ostream 类似但零分配。
class LogStream {
public:
    static constexpr size_t kSmallBuffer = 4096;
    static constexpr size_t kLargeBuffer = 4 * 1024 * 1024;

    using Buffer = FixedBuffer<kSmallBuffer>;

    LogStream& operator<<(bool value);
    LogStream& operator<<(short value);
    LogStream& operator<<(unsigned short value);
    LogStream& operator<<(int value);
    LogStream& operator<<(unsigned int value);
    LogStream& operator<<(long value);
    LogStream& operator<<(unsigned long value);
    LogStream& operator<<(long long value);
    LogStream& operator<<(unsigned long long value);
    LogStream& operator<<(float value);
    LogStream& operator<<(double value);
    LogStream& operator<<(char value);
    LogStream& operator<<(const char* value);
    LogStream& operator<<(const unsigned char* value);
    LogStream& operator<<(const std::string& value);
    LogStream& operator<<(const Buffer& value);
    LogStream& operator<<(const void* value);

    const Buffer& buffer() const { return buffer_; }
    void resetBuffer() { buffer_.reset(); }

private:
    template <typename T>
    void formatInteger(T value);

    static constexpr size_t kMaxNumericSize = 48;
    Buffer buffer_;
};

}  // namespace myself
