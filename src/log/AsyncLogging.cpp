#include "myself/log/AsyncLogging.h"

#include <chrono>
#include <utility>

namespace myself {

AsyncLogging::AsyncLogging(std::string baseName, size_t rollSizeBytes,
                           double flushIntervalSeconds)
    : baseName_(std::move(baseName)),
      rollSize_(rollSizeBytes),
      flushIntervalSeconds_(flushIntervalSeconds),
      current_(new Buffer),
      next_(new Buffer),
      logFile_(baseName_, rollSizeBytes, flushIntervalSeconds) {
    current_->reset();
    next_->reset();
    buffers_.reserve(16);
}

AsyncLogging::~AsyncLogging() {
    stop();
}

void AsyncLogging::start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            return;
        }
        running_ = true;
    }
    thread_ = std::thread([this] { threadFunc(); });
}

void AsyncLogging::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    cv_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void AsyncLogging::append(const char* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (current_->avail() > len) {
        current_->append(data, len);
        return;
    }

    // 当前缓冲写满：换一块，把满的交给后台线程
    buffers_.push_back(std::move(current_));
    if (next_ != nullptr) {
        current_ = std::move(next_);
    } else {
        current_.reset(new Buffer);  // 后台线程还没归还，临时新分配一块
        current_->reset();
    }
    current_->append(data, len);
    cv_.notify_one();
}

void AsyncLogging::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_ != nullptr && current_->length() > 0) {
        logFile_.append(current_->data(), current_->length());
        current_->reset();
    }
    logFile_.flush();
}

void AsyncLogging::threadFunc() {
    BufferPtr newBuffer1(new Buffer);
    BufferPtr newBuffer2(new Buffer);
    newBuffer1->reset();
    newBuffer2->reset();

    std::vector<BufferPtr> buffersToWrite;
    buffersToWrite.reserve(16);

    while (running_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (buffers_.empty()) {
                cv_.wait_for(lock, std::chrono::duration<double>(flushIntervalSeconds_),
                             [this] { return !buffers_.empty() || !running_; });
            }

            buffers_.push_back(std::move(current_));
            current_ = std::move(newBuffer1);
            if (current_ == nullptr) {
                current_.reset(new Buffer);
                current_->reset();
            }
            buffersToWrite.swap(buffers_);
            if (next_ == nullptr) {
                next_ = std::move(newBuffer2);
            }
        }

        for (const auto& buffer : buffersToWrite) {
            logFile_.append(buffer->data(), buffer->length());
        }
        logFile_.flush();

        // 回收两块缓冲复用，其余释放
        if (buffersToWrite.size() > 2) {
            buffersToWrite.resize(2);
        }
        if (newBuffer1 == nullptr && !buffersToWrite.empty()) {
            newBuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer1->reset();
        }
        if (newBuffer2 == nullptr && !buffersToWrite.empty()) {
            newBuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer2->reset();
        }
        buffersToWrite.clear();

        if (newBuffer1 == nullptr) {
            newBuffer1.reset(new Buffer);
            newBuffer1->reset();
        }
        if (newBuffer2 == nullptr) {
            newBuffer2.reset(new Buffer);
            newBuffer2->reset();
        }
    }

    // 退出前把剩余日志写完
    {
        std::lock_guard<std::mutex> lock(mutex_);
        buffers_.push_back(std::move(current_));
        for (const auto& buffer : buffers_) {
            if (buffer != nullptr) {
                logFile_.append(buffer->data(), buffer->length());
            }
        }
        buffers_.clear();
    }
    logFile_.flush();
}

}  // namespace myself
