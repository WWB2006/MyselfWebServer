#include "myself/net/Buffer.h"

#include <sys/uio.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace myself {

void Buffer::retrieve(size_t len) {
    if (len >= readableBytes()) {
        retrieveAll();
        return;
    }
    readIndex_ += len;
}

void Buffer::retrieveAll() {
    readIndex_ = kPrependSize;
    writeIndex_ = kPrependSize;
}

std::string Buffer::retrieveAsString(size_t len) {
    const size_t n = std::min(len, readableBytes());
    std::string result(peek(), n);
    retrieve(n);
    return result;
}

void Buffer::ensureWritableBytes(size_t len) {
    if (writableBytes() < len) {
        makeSpace(len);
    }
}

void Buffer::append(const char* data, size_t len) {
    ensureWritableBytes(len);
    std::copy(data, data + len, beginWrite());
    hasWritten(len);
}

void Buffer::makeSpace(size_t len) {
    // 前面的已消费空间加上尾部剩余空间够用，就整体前移，否则扩容
    if (writableBytes() + prependableBytes() < len + kPrependSize) {
        buffer_.resize(writeIndex_ + len);
    } else {
        const size_t readable = readableBytes();
        std::copy(begin() + readIndex_, begin() + writeIndex_, begin() + kPrependSize);
        readIndex_ = kPrependSize;
        writeIndex_ = readIndex_ + readable;
    }
}

ssize_t Buffer::readFd(int fd, int* savedErrno) {
    // 预留 64KB 栈空间，避免每次读取都扩容
    char extra[65536];
    struct iovec vec[2];
    const size_t writable = writableBytes();

    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extra;
    vec[1].iov_len = sizeof(extra);

    const int iovCount = (writable < sizeof(extra)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovCount);
    if (n < 0) {
        *savedErrno = errno;
        return -1;
    }
    if (static_cast<size_t>(n) <= writable) {
        writeIndex_ += static_cast<size_t>(n);
    } else {
        writeIndex_ = buffer_.size();
        append(extra, static_cast<size_t>(n) - writable);
    }
    return n;
}

}  // namespace myself

