#pragma once

#include <sys/epoll.h>

#include <vector>

namespace myself {

class Channel;

/// epoll 的最小封装：注册、修改、删除与等待事件。
class Epoller {
public:
    explicit Epoller(int maxEvents = 1024);
    ~Epoller();

    Epoller(const Epoller&) = delete;
    Epoller& operator=(const Epoller&) = delete;

    bool add(Channel* channel);
    bool modify(Channel* channel);
    bool remove(Channel* channel);

    /// 返回就绪事件数；-1 表示出错，0 表示超时。
    int wait(int timeoutMs);

    const std::vector<epoll_event>& events() const { return events_; }

private:
    int epollFd_{-1};
    std::vector<epoll_event> events_;
};

}  // namespace myself

