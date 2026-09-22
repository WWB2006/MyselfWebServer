#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace myself {

/// 线性缓冲区：读指针之前是已消费空间，必要时把剩余数据前移。
/// 只在所属线程使用，不做内部加锁。
class Buffer {
public:
    static constexpr size_t kInitialSize = 1024;
    static constexpr size_t kPrependSize = 8;  // 预留给后续长度字段/头部

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kPrependSize + initialSize) {
        readIndex_ = kPrependSize;
        writeIndex_ = kPrependSize;
    }

    size_t readableBytes() const { return writeIndex_ - readIndex_; }
    size_t writableBytes() const { return buffer_.size() - writeIndex_; }
    size_t prependableBytes() const { return readIndex_; }

    const char* peek() const { return begin() + readIndex_; }
    char* beginWrite() { return begin() + writeIndex_; }
    const char* beginWrite() const { return begin() + writeIndex_; }

    /// 消费 len 字节。
    void retrieve(size_t len);
    void retrieveAll();

    std::string retrieveAsString(size_t len);
    std::string retrieveAllAsString() { return retrieveAsString(readableBytes()); }

    void append(const char* data, size_t len);
    void append(const std::string& data) { append(data.data(), data.size()); }

    void ensureWritableBytes(size_t len);

    /// 追加写入后必须调用，用于推进写指针。
    void hasWritten(size_t len) { writeIndex_ += len; }

    /// 从 fd 读取数据，返回实际读取字节数；出错时把 errno 写入 savedErrno 并返回 -1。
    ssize_t readFd(int fd, int* savedErrno);

private:
    char* begin() { return buffer_.data(); }
    const char* begin() const { return buffer_.data(); }
    void makeSpace(size_t len);

    std::vector<char> buffer_;
    size_t readIndex_{0};
    size_t writeIndex_{0};
};

}  // namespace myself

