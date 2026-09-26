#include "myself/net/EventLoopThreadPool.h"

#include <utility>

#include "myself/log/Logger.h"
#include "myself/net/EventLoop.h"

namespace myself {

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, std::string name)
    : baseLoop_(baseLoop), name_(std::move(name)) {}

EventLoopThreadPool::~EventLoopThreadPool() {
    // 先让所有子循环退出，再回收线程
    for (auto& thread : threads_) {
        if (thread->loop() != nullptr) {
            thread->loop()->quit();
        }
    }
    for (auto& thread : threads_) {
        thread->join();
    }
}

void EventLoopThreadPool::start(const EventLoopThread::ThreadInitCallback& callback) {
    if (started_) {
        return;
    }
    started_ = true;
    baseLoop_->assertInLoopThread();

    threads_.reserve(threadCount_);
    loops_.reserve(threadCount_);

    for (size_t i = 0; i < threadCount_; ++i) {
        const std::string threadName = name_ + "-" + std::to_string(i);
        auto thread = std::make_unique<EventLoopThread>(threadName, callback);
        loops_.push_back(thread->startLoop());
        threads_.push_back(std::move(thread));
    }

    if (threadCount_ == 0 && callback) {
        callback(baseLoop_);  // 单线程模式下也用 baseLoop 跑初始化
    }

    LOG_INFO << "io pool " << name_ << " started with " << loops_.size() << " thread(s)";
}

EventLoop* EventLoopThreadPool::nextLoop() {
    baseLoop_->assertInLoopThread();

    if (loops_.empty()) {
        return baseLoop_;  // 线程数为 0：退化为主线程单 Reactor
    }
    EventLoop* loop = loops_[next_];
    next_ = (next_ + 1) % loops_.size();
    return loop;
}

}  // namespace myself
