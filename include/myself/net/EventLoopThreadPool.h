#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "myself/net/EventLoopThread.h"

namespace myself {

class EventLoop;

/// 主从 Reactor 中的“从”部分：管理若干 EventLoopThread，按轮询分配新连接。
class EventLoopThreadPool {
public:
    explicit EventLoopThreadPool(EventLoop* baseLoop, std::string name = "pool");
    ~EventLoopThreadPool();

    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

    void setThreadCount(size_t count) { threadCount_ = count; }
    size_t threadCount() const { return threadCount_; }

    /// 启动所有子线程的事件循环；线程数为 0 时退化为单线程，只用 baseLoop。
    void start(const EventLoopThread::ThreadInitCallback& callback = {});

    /// 轮询取下一个子循环，只应在 baseLoop 线程调用。
    EventLoop* nextLoop();

    EventLoop* baseLoop() const { return baseLoop_; }
    const std::vector<EventLoop*>& loops() const { return loops_; }

private:
    EventLoop* baseLoop_;
    std::string name_;
    bool started_{false};
    size_t threadCount_{0};
    size_t next_{0};
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};

}  // namespace myself
