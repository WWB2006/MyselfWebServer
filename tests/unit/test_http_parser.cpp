#include <string>

#include <gtest/gtest.h>

#include "myself/http/HttpParser.h"
#include "myself/http/HttpRequest.h"
#include "myself/net/Buffer.h"

namespace {

using myself::Buffer;
using myself::HttpParser;
using myself::HttpRequest;

}  // namespace

TEST(HttpParserTest, ParsesSimpleGetRequest) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n"));

    ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
    const HttpRequest request = parser.takeRequest();

    EXPECT_EQ(HttpRequest::Method::kGet, request.method());
    EXPECT_EQ("/index.html", request.path());
    EXPECT_EQ("localhost", request.getHeader("host"));
    EXPECT_EQ("HTTP/1.1", request.version());
    EXPECT_TRUE(request.isKeepAlive());
    EXPECT_EQ(0u, buffer.readableBytes());
}

TEST(HttpParserTest, SplitsTargetAndQuery) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("GET /api/stats?device=plc-01&range=1h HTTP/1.1\r\n\r\n"));

    ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
    const HttpRequest request = parser.takeRequest();

    EXPECT_EQ("/api/stats", request.path());
    EXPECT_EQ("device=plc-01&range=1h", request.query());
}

TEST(HttpParserTest, NeedMoreWhenRequestLineIsPartial) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("GET /index"));
    EXPECT_EQ(HttpParser::Result::kNeedMore, parser.parse(&buffer));

    buffer.append(std::string(".html HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
    EXPECT_EQ("/index.html", parser.takeRequest().path());
}

TEST(HttpParserTest, NeedMoreWhenHeadersArePartial) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("GET / HTTP/1.1\r\nHost: x\r\n"));
    EXPECT_EQ(HttpParser::Result::kNeedMore, parser.parse(&buffer));

    buffer.append(std::string("\r\n"));
    EXPECT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
}

TEST(HttpParserTest, ParsesPipelinedRequestsInOneBuffer) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("GET /a HTTP/1.1\r\nHost: x\r\n\r\n"
                              "GET /b HTTP/1.1\r\nHost: x\r\n\r\n"));

    ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
    EXPECT_EQ("/a", parser.takeRequest().path());

    ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
    EXPECT_EQ("/b", parser.takeRequest().path());
    EXPECT_EQ(0u, buffer.readableBytes());
}

TEST(HttpParserTest, ParsesPostBodyByContentLength) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(
        std::string("POST /api/data HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello"));

    ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
    const HttpRequest request = parser.takeRequest();

    EXPECT_EQ(HttpRequest::Method::kPost, request.method());
    EXPECT_EQ("/api/data", request.path());
    EXPECT_EQ("hello", request.body());
}

TEST(HttpParserTest, WaitsUntilWholeBodyArrives) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("POST /x HTTP/1.1\r\nContent-Length: 10\r\n\r\nabc"));
    EXPECT_EQ(HttpParser::Result::kNeedMore, parser.parse(&buffer));

    buffer.append(std::string("defghij"));
    ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
    EXPECT_EQ("abcdefghij", parser.takeRequest().body());
}

TEST(HttpParserTest, DefaultKeepAliveDependsOnVersion) {
    {
        Buffer buffer;
        HttpParser parser;
        buffer.append(std::string("GET / HTTP/1.0\r\n\r\n"));
        ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
        EXPECT_FALSE(parser.takeRequest().isKeepAlive());
    }
    {
        Buffer buffer;
        HttpParser parser;
        buffer.append(std::string("GET / HTTP/1.1\r\n\r\n"));
        ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
        EXPECT_TRUE(parser.takeRequest().isKeepAlive());
    }
}

TEST(HttpParserTest, ConnectionHeaderOverridesDefault) {
    {
        Buffer buffer;
        HttpParser parser;
        buffer.append(std::string("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
        ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
        EXPECT_TRUE(parser.takeRequest().isKeepAlive());
    }
    {
        Buffer buffer;
        HttpParser parser;
        buffer.append(std::string("GET / HTTP/1.1\r\nConnection: close\r\n\r\n"));
        ASSERT_EQ(HttpParser::Result::kComplete, parser.parse(&buffer));
        EXPECT_FALSE(parser.takeRequest().isKeepAlive());
    }
}

TEST(HttpParserTest, RejectsMalformedRequestLine) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("BADREQUEST\r\n\r\n"));
    EXPECT_EQ(HttpParser::Result::kBadRequest, parser.parse(&buffer));
}

TEST(HttpParserTest, RejectsUnsupportedMethod) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("DELETE /x HTTP/1.1\r\n\r\n"));
    EXPECT_EQ(HttpParser::Result::kBadRequest, parser.parse(&buffer));
}

TEST(HttpParserTest, RejectsUnsupportedVersion) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("GET / HTTP/2.0\r\n\r\n"));
    EXPECT_EQ(HttpParser::Result::kBadRequest, parser.parse(&buffer));
}

TEST(HttpParserTest, RejectsNonNumericContentLength) {
    Buffer buffer;
    HttpParser parser;
    buffer.append(std::string("POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n"));
    EXPECT_EQ(HttpParser::Result::kBadRequest, parser.parse(&buffer));
}

TEST(HttpParserTest, RejectsOversizedHeaderBlock) {
    Buffer buffer;
    HttpParser parser;
    std::string huge = "GET / HTTP/1.1\r\n";
    huge += "X-Long: " + std::string(9000, 'a') + "\r\n\r\n";
    buffer.append(huge);
    EXPECT_EQ(HttpParser::Result::kBadRequest, parser.parse(&buffer));
}
