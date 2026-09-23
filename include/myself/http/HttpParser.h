#pragma once

#include <cstddef>
#include <string>

#include "myself/http/HttpRequest.h"

namespace myself {

class Buffer;

/// HTTP/1.1 请求解析器，基于有限状态机。
/// 只接收字节数组、不关心 socket，因此可以脱离网络单独做单元测试。
class HttpParser {
public:
    enum class Result {
        kNeedMore,   // 数据不完整，等待下一次读取
        kComplete,   // 已解析出一条完整请求
        kBadRequest  // 报文非法，调用方应返回 400 并关闭连接
    };

    explicit HttpParser(size_t maxHeaderBytes = 8192, size_t maxBodyBytes = 1 << 20)
        : maxHeaderBytes_(maxHeaderBytes), maxBodyBytes_(maxBodyBytes) {}

    /// 从缓冲区解析；返回 kComplete 后可用 takeRequest() 取走结果。
    Result parse(Buffer* buffer);

    bool hasRequest() const { return state_ == State::kGotAll; }

    /// 取出已完成的请求并重置状态，便于处理流水线（pipelining）请求。
    HttpRequest takeRequest();
    void reset();

private:
    enum class State { kRequestLine, kHeaders, kBody, kGotAll, kError };

    Result parseRequestLine(Buffer* buffer);
    Result parseHeaders(Buffer* buffer);
    Result parseBody(Buffer* buffer);

    static const char* findCRLF(const char* begin, const char* end);

    State state_{State::kRequestLine};
    size_t maxHeaderBytes_;
    size_t maxBodyBytes_;
    size_t contentLength_{0};
    size_t parsedHeaderBytes_{0};
    HttpRequest request_;
};

}  // namespace myself
