#include "myself/log/LogStream.h"

#include <cstdio>
#include <string>

namespace myself {

namespace {

const char kDigits[] = "9876543210123456789";
const char* kZeroDigit = kDigits + 9;

}  // namespace

template <typename T>
void LogStream::formatInteger(T value) {
    if (buffer_.avail() < kMaxNumericSize) {
        return;
    }
    char* end = buffer_.current() + kMaxNumericSize;
    char* start = end;
    const bool negative = value < 0;
    do {
        const int digit = static_cast<int>(value % 10);
        value /= 10;
        *(--start) = kZeroDigit[digit];
    } while (value != 0);
    if (negative) {
        *(--start) = '-';
    }
    const size_t len = static_cast<size_t>(end - start);
    buffer_.append(start, len);
}

LogStream& LogStream::operator<<(bool value) {
    buffer_.append(value ? "1" : "0", 1);
    return *this;
}

LogStream& LogStream::operator<<(short value) {
    formatInteger(value);
    return *this;
}

LogStream& LogStream::operator<<(unsigned short value) {
    formatInteger(value);
    return *this;
}

LogStream& LogStream::operator<<(int value) {
    formatInteger(value);
    return *this;
}

LogStream& LogStream::operator<<(unsigned int value) {
    formatInteger(value);
    return *this;
}

LogStream& LogStream::operator<<(long value) {
    formatInteger(value);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long value) {
    formatInteger(value);
    return *this;
}

LogStream& LogStream::operator<<(long long value) {
    formatInteger(value);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long long value) {
    formatInteger(value);
    return *this;
}

LogStream& LogStream::operator<<(float value) {
    return operator<<(static_cast<double>(value));
}

LogStream& LogStream::operator<<(double value) {
    if (buffer_.avail() >= kMaxNumericSize) {
        char text[kMaxNumericSize];
        const int len = std::snprintf(text, sizeof(text), "%.6g", value);
        if (len > 0) {
            buffer_.append(text, static_cast<size_t>(len));
        }
    }
    return *this;
}

LogStream& LogStream::operator<<(char value) {
    buffer_.append(&value, 1);
    return *this;
}

LogStream& LogStream::operator<<(const char* value) {
    if (value != nullptr) {
        buffer_.append(value, std::strlen(value));
    } else {
        buffer_.append("(null)", 6);
    }
    return *this;
}

LogStream& LogStream::operator<<(const unsigned char* value) {
    return operator<<(reinterpret_cast<const char*>(value));
}

LogStream& LogStream::operator<<(const std::string& value) {
    buffer_.append(value.data(), value.size());
    return *this;
}

LogStream& LogStream::operator<<(const Buffer& value) {
    buffer_.append(value.data(), value.length());
    return *this;
}

LogStream& LogStream::operator<<(const void* value) {
    if (buffer_.avail() >= kMaxNumericSize) {
        char text[kMaxNumericSize];
        const int len = std::snprintf(text, sizeof(text), "%p", value);
        if (len > 0) {
            buffer_.append(text, static_cast<size_t>(len));
        }
    }
    return *this;
}

}  // namespace myself
