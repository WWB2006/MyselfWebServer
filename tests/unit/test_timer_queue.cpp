#include <gtest/gtest.h>

#include "myself/net/EventLoop.h"
#include "myself/timer/TimerQueue.h"

TEST(TimerQueueTest, RunAfterFiresOnce) {
    myself::EventLoop loop;
    int fired = 0;

    loop.runAfter(0.02, [&fired] { ++fired; });
    loop.runAfter(0.15, [&loop] { loop.quit(); });
    loop.loop();

    EXPECT_EQ(1, fired);
}

TEST(TimerQueueTest, CancelPreventsCallback) {
    myself::EventLoop loop;
    int fired = 0;

    const myself::TimerId id = loop.runAfter(0.03, [&fired] { ++fired; });
    loop.cancelTimer(id);
    loop.runAfter(0.12, [&loop] { loop.quit(); });
    loop.loop();

    EXPECT_EQ(0, fired);
}

TEST(TimerQueueTest, RepeatFiresMultipleTimesUntilCancelled) {
    myself::EventLoop loop;
    int fired = 0;

    const myself::TimerId id = loop.runEvery(0.02, [&fired] { ++fired; });
    loop.runAfter(0.13, [&loop, &id] {
        loop.cancelTimer(id);
        loop.quit();
    });
    loop.loop();

    EXPECT_GE(fired, 3);
}

TEST(TimerQueueTest, CancelInsideCallbackStopsRepeatingTimer) {
    myself::EventLoop loop;
    int fired = 0;
    myself::TimerId id;

    id = loop.runEvery(0.02, [&fired, &loop, &id] {
        ++fired;
        if (fired >= 2) {
            loop.cancelTimer(id);  // 重复定时器在自己回调里取消自己
            loop.quit();
        }
    });
    loop.runAfter(0.5, [&loop] { loop.quit(); });  // 兜底，避免用例挂死
    loop.loop();

    EXPECT_EQ(2, fired);
}

TEST(TimerQueueTest, NextTimeoutReflectsPendingTimer) {
    myself::EventLoop loop;
    myself::TimerQueue queue(&loop);

    EXPECT_EQ(-1, queue.nextTimeoutMs());  // 没有定时器：可以一直阻塞

    queue.addTimer([] {}, myself::now() + myself::secondsToDuration(1.0), 0.0);
    const int timeout = queue.nextTimeoutMs();
    EXPECT_GT(timeout, 0);
    EXPECT_LE(timeout, 1000);
    EXPECT_EQ(1u, queue.size());
}

TEST(TimerQueueTest, ExpiredTimersRunOnHandleExpired) {
    myself::EventLoop loop;
    myself::TimerQueue queue(&loop);
    int fired = 0;

    queue.addTimer([&fired] { ++fired; }, myself::now(), 0.0);
    EXPECT_EQ(0, queue.nextTimeoutMs());  // 已到期，应该立刻处理

    queue.handleExpiredTimers(myself::now());
    EXPECT_EQ(1, fired);
    EXPECT_EQ(0u, queue.size());
}
