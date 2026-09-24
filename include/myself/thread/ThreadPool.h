#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace myself {

/// 通用计算线程池：任务队列 + 条件变量 + 工作线程。
/// 用于把耗时的任务（文件读取、统计计算）从 IO 线程挪走；
/// 任务里不要直接写 socket，回写要通过 EventLoop::runInLoop 回到 IO 线程。
class ThreadPool {
public:
    explicit ThreadPool(size_t threadCount = 0, std::string name = "worker");
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void start();
    void stop();

    /// 投递任务；线程池未启动时抛 std::runtime_error。
    void submit(std::function<void()> task);

    size_t pendingTasks() const;
    size_t threadCount() const { return threadCount_; }
    const std::string& name() const { return name_; }

private:
    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::thread> threads_;
    std::deque<std::function<void()>> tasks_;
    size_t threadCount_;
    std::string name_;
    bool running_{false};
};

}  // namespace myself
