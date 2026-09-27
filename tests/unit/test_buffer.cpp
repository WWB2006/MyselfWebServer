#include <unistd.h>

#include <string>

#include <gtest/gtest.h>

#include "myself/net/Buffer.h"

namespace {

using myself::Buffer;

}  // namespace

TEST(BufferTest, AppendThenRetrieveAll) {
    Buffer buffer;
    buffer.append("hello", 5);

    EXPECT_EQ(5u, buffer.readableBytes());
    EXPECT_EQ("hello", buffer.retrieveAllAsString());
    EXPECT_EQ(0u, buffer.readableBytes());
}

TEST(BufferTest, RetrievePartialKeepsRemainder) {
    Buffer buffer;
    buffer.append(std::string("hello world"));

    EXPECT_EQ("hello", buffer.retrieveAsString(5));
    EXPECT_EQ(6u, buffer.readableBytes());
    EXPECT_EQ(" world", buffer.retrieveAllAsString());
}

TEST(BufferTest, RetrieveMoreThanAvailableClearsBuffer) {
    Buffer buffer;
    buffer.append(std::string("abc"));

    EXPECT_EQ("abc", buffer.retrieveAsString(99));
    EXPECT_EQ(0u, buffer.readableBytes());
}

TEST(BufferTest, GrowsWhenAppendingLargeBlock) {
    Buffer buffer(8);
    const std::string payload(4096, 'x');
    buffer.append(payload);

    EXPECT_EQ(payload.size(), buffer.readableBytes());
    EXPECT_EQ(payload, buffer.retrieveAllAsString());
}

TEST(BufferTest, ReusesConsumedSpace) {
    Buffer buffer(32);
    buffer.append(std::string(16, 'a'));
    buffer.retrieve(16);

    // 空间被前移复用，读取内容仍然正确
    buffer.append(std::string(24, 'b'));
    EXPECT_EQ(std::string(24, 'b'), buffer.retrieveAllAsString());
}

TEST(BufferTest, ReadFdFromPipe) {
    int fds[2] = {-1, -1};
    ASSERT_EQ(0, ::pipe(fds));

    const char text[] = "abcdef";
    ASSERT_EQ(6, static_cast<int>(::write(fds[1], text, 6)));

    Buffer buffer;
    int savedErrno = 0;
    const ssize_t n = buffer.readFd(fds[0], &savedErrno);

    EXPECT_EQ(6, n);
    EXPECT_EQ("abcdef", buffer.retrieveAllAsString());

    ::close(fds[0]);
    ::close(fds[1]);
}
