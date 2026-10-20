#pragma once

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <memory>

namespace myself {

/// 一段待发送的文件区间（fd + 偏移 + 长度），用于 sendfile(2) 零拷贝。
///
/// 为什么用 shared_ptr 持有而不是裸 fd：
/// HttpResponse 会在 worker 线程构造、再按值传到 IO 线程发送，中间有好几份拷贝。
/// 裸 fd 配析构函数会导致重复 close（甚至关掉别的连接刚复用的 fd）；
/// 共享所有权让"最后一份析构时关闭"成为唯一语义。
class FileBody {
public:
    FileBody(int fd, off_t offset, size_t length)
        : fd_(fd), offset_(offset), length_(length) {}

    ~FileBody() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    FileBody(const FileBody&) = delete;
    FileBody& operator=(const FileBody&) = delete;

    int fd() const { return fd_; }
    off_t offset() const { return offset_; }
    size_t length() const { return length_; }

private:
    int fd_;
    off_t offset_;
    size_t length_;
};

using FileBodyPtr = std::shared_ptr<FileBody>;

}  // namespace myself
