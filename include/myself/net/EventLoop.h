#pragma once

#include <sys/epoll.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace myself {

class Channel;
class Epoller;

/// Reactor 事件循环。
/// 阶段 2 的单线程用法保持不变；阶段 4 起支持跨线程投递任务，
/// 因此主线程可以只做 Acceptor，子线程各跑一个循环处理读写。
class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /// timeoutMs 为 -1 表示一直阻塞等待（可通过 quit 或 wakeup 唤醒）。
    void loop(int timeoutMs = -1);
    void quit();

    /// 在所属线程执行；若当前不在该线程，则入队并唤醒该线程。
    void runInLoop(Functor cb);
    void queueInLoop(Functor cb);

    bool isInLoopThread() const { return threadId_ == std::this_thread::get_id(); }

    /// 违反线程归属时打印日志并终止，用于尽早暴露跨线程操作。
    void assertInLoopThread() const;

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    size_t channelCount() const { return channelCount_; }
    size_t pendingTaskCount();

    /// 线程名：只用于日志与调试，创建后到 loop() 之前设置。
    void setName(std::string name) { name_ = std::move(name); }
    const std::string& name() const { return name_; }

private:
    void dispatch(const std::vector<epoll_event>& events, int count);
    void wakeup();
    void handleWakeup();
    void doPendingFunctors();

    std::unique_ptr<Epoller> epoller_;
    std::unique_ptr<Channel> wakeupChannel_;
    int wakeupFd_{-1};
    std::map<int, Channel*> channels_;
    std::thread::id threadId_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> callingPendingFunctors_{false};
    std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;
    size_t channelCount_{0};
    std::string name_;
};

}  // namespace myself
