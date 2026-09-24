#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace myself {

class EventLoop;

/// 一个线程 + 一个事件循环。
/// startLoop() 会等循环真正跑起来（线程完成初始化）再返回，避免拿到空指针。
class EventLoopThread {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    explicit EventLoopThread(std::string name = "io", ThreadInitCallback callback = {});
    ~EventLoopThread();

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    /// 启动线程并返回其中的事件循环指针。
    EventLoop* startLoop();

    /// 等待线程结束（应先调用 loop()->quit()）。
    void join();

    EventLoop* loop() const { return loop_; }
    const std::string& name() const { return name_; }

private:
    void threadFunc();

    std::string name_;
    ThreadInitCallback initCallback_;
    EventLoop* loop_{nullptr};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

}  // namespace myself
