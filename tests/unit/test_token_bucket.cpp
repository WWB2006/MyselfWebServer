#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "myself/util/TokenBucket.h"

namespace {

TEST(TokenBucketTest, AlwaysAllowsWhenDisabled) {
    myself::TokenBucket limiter(0.0, 0.0);
    EXPECT_FALSE(limiter.isEnabled());

    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(limiter.tryAcquire());
    }
}

TEST(TokenBucketTest, AllowsBurstUpToCapacity) {
    // 限制 10 QPS，突发容量 5
    myself::TokenBucket limiter(10.0, 5.0);
    EXPECT_TRUE(limiter.isEnabled());
    EXPECT_DOUBLE_EQ(10.0, limiter.rate());
    EXPECT_DOUBLE_EQ(5.0, limiter.burst());

    // 突发容量内允许 5 个连续请求
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.tryAcquire()) << "Failed at request " << i;
    }

    // 第 6 个立即请求因令牌耗尽而被拒绝
    EXPECT_FALSE(limiter.tryAcquire());
}

TEST(TokenBucketTest, RefillsTokensOverTime) {
    // 限制 100 QPS (10ms 补充 1 个令牌)，容量 5
    myself::TokenBucket limiter(100.0, 5.0);

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(limiter.tryAcquire());
    }
    EXPECT_FALSE(limiter.tryAcquire());

    // 等待 35ms，理论上补充 3 个令牌
    std::this_thread::sleep_for(std::chrono::milliseconds(35));

    // 至少能成功获取 3 个令牌
    EXPECT_TRUE(limiter.tryAcquire());
    EXPECT_TRUE(limiter.tryAcquire());
    EXPECT_TRUE(limiter.tryAcquire());
}

TEST(TokenBucketTest, ClampsTokensAtBurstCapacity) {
    // 限制 1000 QPS，容量 10
    myself::TokenBucket limiter(1000.0, 10.0);

    // 等待 50ms (本可生成 50 个令牌)，但不能突破容量上限 10
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_LE(limiter.availableTokens(), 10.0);

    // 只能获取 10 个
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.tryAcquire());
    }
    EXPECT_FALSE(limiter.tryAcquire());
}

TEST(TokenBucketTest, HandlesConcurrentThreadsSafely) {
    // 突发 100，速率 10 QPS
    myself::TokenBucket limiter(10.0, 100.0);

    std::atomic<int> successCount{0};
    std::atomic<int> rejectCount{0};

    const int kThreadCount = 8;
    const int kRequestsPerThread = 50;  // 总共 400 个请求并发争抢 100 个令牌

    std::vector<std::thread> workers;
    for (int t = 0; t < kThreadCount; ++t) {
        workers.emplace_back([&limiter, &successCount, &rejectCount] {
            for (int r = 0; r < kRequestsPerThread; ++r) {
                if (limiter.tryAcquire()) {
                    successCount.fetch_add(1);
                } else {
                    rejectCount.fetch_add(1);
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    // 允许通过的请求数一定介于 100 到 105 之间（考虑并发毫秒时间内的微量补发）
    EXPECT_GE(successCount.load(), 100);
    EXPECT_LE(successCount.load(), 110);
    EXPECT_EQ(400, successCount.load() + rejectCount.load());
}

}  // namespace
