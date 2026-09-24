#include "myself/thread/ThreadPool.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace myself {

ThreadPool::ThreadPool(size_t threadCount, std::string name)
    : threadCount_(threadCount), name_(std::move(name)) {}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return;
    }
    running_ = true;

    threads_.reserve(threadCount_);
    for (size_t i = 0; i < threadCount_; ++i) {
        threads_.emplace_back([this] { workerLoop(); });
    }

    std::cout << "[pool] " << name_ << " worker pool started with " << threadCount_
              << " thread(s)\n";
}

void ThreadPool::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;  // 队列里的剩余任务仍会在退出前执行完
    }
    cv_.notify_all();
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            throw std::runtime_error("thread pool [" + name_ + "] is not running");
        }
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

size_t ThreadPool::pendingTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || !tasks_.empty(); });

            if (tasks_.empty()) {
                if (!running_) {
                    return;  // 已停止且没有剩余任务
                }
                continue;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        try {
            task();
        } catch (const std::exception& ex) {
            // 单个任务异常不能让工作线程退出
            std::cerr << "[pool:" << name_ << "] task failed: " << ex.what() << "\n";
        } catch (...) {
            std::cerr << "[pool:" << name_ << "] task failed: unknown exception\n";
        }
    }
}

}  // namespace myself
