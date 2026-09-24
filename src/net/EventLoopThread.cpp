#include "myself/net/EventLoopThread.h"

#include <utility>

#include "myself/net/EventLoop.h"

namespace myself {

EventLoopThread::EventLoopThread(std::string name, ThreadInitCallback callback)
    : name_(std::move(name)), initCallback_(std::move(callback)) {}

EventLoopThread::~EventLoopThread() {
    if (loop_ != nullptr) {
        loop_->quit();
    }
    join();
}

EventLoop* EventLoopThread::startLoop() {
    thread_ = std::thread([this] { threadFunc(); });

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return loop_ != nullptr; });
    return loop_;
}

void EventLoopThread::threadFunc() {
    EventLoop loop;          // 在本线程栈上创建，生命周期与线程一致
    loop.setName(name_);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
    }
    cv_.notify_one();

    if (initCallback_) {
        initCallback_(&loop);
    }

    loop.loop();             // 退出后 EventLoop 在本线程析构

    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}

void EventLoopThread::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace myself
