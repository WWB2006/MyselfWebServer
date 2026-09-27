#include <string>

#include <gtest/gtest.h>

#include "myself/log/LogStream.h"

TEST(LogStreamTest, FormatsIntegersDoublesAndBooleans) {
    myself::LogStream stream;
    stream << "count=" << 42 << " negative=" << -7 << " ratio=" << 0.5 << " flag=" << true;

    EXPECT_EQ("count=42 negative=-7 ratio=0.5 flag=1", stream.buffer().toString());
}

TEST(LogStreamTest, FormatsUnsignedAndLargeValues) {
    myself::LogStream stream;
    const uint64_t big = 1234567890123ULL;
    stream << "big=" << big;

    EXPECT_EQ("big=1234567890123", stream.buffer().toString());
}

TEST(LogStreamTest, HandlesNullCharPointer) {
    myself::LogStream stream;
    const char* nullText = nullptr;
    stream << nullText;

    EXPECT_EQ("(null)", stream.buffer().toString());
}

TEST(LogStreamTest, AppendsStandardString) {
    myself::LogStream stream;
    stream << std::string("hello") << " " << std::string("world");

    EXPECT_EQ("hello world", stream.buffer().toString());
}

TEST(LogStreamTest, ResetBufferClearsContent) {
    myself::LogStream stream;
    stream << "abc";
    ASSERT_EQ(3u, stream.buffer().length());

    stream.resetBuffer();
    EXPECT_EQ(0u, stream.buffer().length());

    stream << "next";
    EXPECT_EQ("next", stream.buffer().toString());
}
