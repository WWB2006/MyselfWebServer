#pragma once

#include <sys/epoll.h>

#include <map>
#include <memory>
#include <vector>

namespace myself {

class Channel;
class Epoller;

/// 单线程 Reactor：负责等待事件并把事件分发给对应的 Channel。
/// 多线程版本（主从 Reactor）在阶段 4 引入。
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /// timeoutMs 为 -1 表示一直阻塞等待。
    void loop(int timeoutMs = -1);
    void quit() { quit_ = true; }

    /// 由 Channel 调用：新增、修改或删除关注的 fd。
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

private:
    void dispatch(const std::vector<epoll_event>& events, int count);

    std::unique_ptr<Epoller> epoller_;
    std::map<int, Channel*> channels_;
    int channelCount_{0};
    bool quit_{false};
};

}  // namespace myself

