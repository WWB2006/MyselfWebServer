#include "myself/http/HttpParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

#include "myself/net/Buffer.h"

namespace myself {

namespace {

const char kCRLF[] = "\r\n";

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(" \t");
    return text.substr(begin, end - begin + 1);
}

std::string toLower(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool isDigitString(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
               return std::isdigit(c) != 0;
           });
}

}  // namespace

const char* HttpParser::findCRLF(const char* begin, const char* end) {
    const char* pos = std::search(begin, end, kCRLF, kCRLF + 2);
    return pos == end ? nullptr : pos;
}

HttpParser::Result HttpParser::parse(Buffer* buffer) {
    if (state_ == State::kError) {
        return Result::kBadRequest;
    }
    if (state_ == State::kGotAll) {
        return Result::kComplete;
    }

    for (;;) {
        if (state_ == State::kRequestLine) {
            const Result result = parseRequestLine(buffer);
            if (result != Result::kNeedMore) {
                state_ = (result == Result::kBadRequest) ? State::kError : State::kGotAll;
                return result;
            }
            if (state_ == State::kRequestLine) {
                return Result::kNeedMore;  // 请求行数据不足
            }
            continue;  // 已进入头部解析
        }

        if (state_ == State::kHeaders) {
            const Result result = parseHeaders(buffer);
            if (result != Result::kNeedMore) {
                state_ = (result == Result::kBadRequest) ? State::kError : State::kGotAll;
                return result;
            }
            if (state_ == State::kHeaders) {
                return Result::kNeedMore;  // 头部数据不足
            }
            continue;  // 已进入请求体解析
        }

        if (state_ == State::kBody) {
            const Result result = parseBody(buffer);
            if (result == Result::kBadRequest) {
                state_ = State::kError;
                return Result::kBadRequest;
            }
            if (result == Result::kComplete) {
                return Result::kComplete;
            }
            return Result::kNeedMore;  // 请求体还没读完
        }

        if (state_ == State::kGotAll) {
            return Result::kComplete;
        }
        return Result::kBadRequest;
    }
}

HttpParser::Result HttpParser::parseRequestLine(Buffer* buffer) {
    const char* begin = buffer->peek();
    const char* end = begin + buffer->readableBytes();
    const char* crlf = findCRLF(begin, end);

    if (crlf == nullptr) {
        if (buffer->readableBytes() > maxHeaderBytes_) {
            return Result::kBadRequest;  // 请求行过长
        }
        return Result::kNeedMore;
    }

    const std::string line(begin, crlf);
    buffer->retrieve(static_cast<size_t>(crlf - begin) + 2);
    parsedHeaderBytes_ += line.size() + 2;
    if (parsedHeaderBytes_ > maxHeaderBytes_) {
        return Result::kBadRequest;
    }

    // 格式：METHOD SP TARGET SP VERSION
    const auto firstSpace = line.find(' ');
    if (firstSpace == std::string::npos) {
        return Result::kBadRequest;
    }
    const auto secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) {
        return Result::kBadRequest;
    }

    const std::string methodText = line.substr(0, firstSpace);
    const std::string target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    const std::string versionText = trim(line.substr(secondSpace + 1));

    const HttpRequest::Method method = HttpRequest::methodFromString(methodText);
    if (method == HttpRequest::Method::kInvalid) {
        return Result::kBadRequest;
    }
    if (versionText != "HTTP/1.1" && versionText != "HTTP/1.0") {
        return Result::kBadRequest;
    }

    request_.setMethod(method);
    request_.setPath(target);
    request_.setVersion(versionText);
    // 默认长连接策略：1.1 保持，1.0 关闭；随后由 Connection 头覆盖
    request_.setKeepAlive(versionText == "HTTP/1.1");

    state_ = State::kHeaders;
    return Result::kNeedMore;
}

HttpParser::Result HttpParser::parseHeaders(Buffer* buffer) {
    for (;;) {
        const char* begin = buffer->peek();
        const char* end = begin + buffer->readableBytes();
        const char* crlf = findCRLF(begin, end);

        if (crlf == nullptr) {
            if (buffer->readableBytes() > maxHeaderBytes_) {
                return Result::kBadRequest;
            }
            return Result::kNeedMore;  // 头部还没接收完整
        }

        if (crlf == begin) {
            buffer->retrieve(2);  // 空行：头部结束
            parsedHeaderBytes_ += 2;
            if (parsedHeaderBytes_ > maxHeaderBytes_) {
                return Result::kBadRequest;
            }
            if (contentLength_ > maxBodyBytes_) {
                return Result::kBadRequest;  // 请求体超限
            }
            if (contentLength_ == 0) {
                state_ = State::kGotAll;
                return Result::kComplete;
            }
            state_ = State::kBody;
            return Result::kNeedMore;
        }

        const std::string line(begin, crlf);
        buffer->retrieve(static_cast<size_t>(crlf - begin) + 2);
        parsedHeaderBytes_ += line.size() + 2;
        if (parsedHeaderBytes_ > maxHeaderBytes_) {
            return Result::kBadRequest;
        }

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            return Result::kBadRequest;
        }
        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));
        if (key.empty()) {
            return Result::kBadRequest;
        }
        request_.addHeader(key, value);

        const std::string lowerKey = toLower(key);
        if (lowerKey == "content-length") {
            if (!isDigitString(value)) {
                return Result::kBadRequest;
            }
            contentLength_ = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (lowerKey == "connection") {
            const std::string lowerValue = toLower(value);
            if (lowerValue.find("close") != std::string::npos) {
                request_.setKeepAlive(false);
            } else if (lowerValue.find("keep-alive") != std::string::npos) {
                request_.setKeepAlive(true);
            }
        }
    }
}

HttpParser::Result HttpParser::parseBody(Buffer* buffer) {
    if (buffer->readableBytes() < contentLength_) {
        return Result::kNeedMore;  // 半包：请求体还没读完
    }
    request_.setBody(buffer->retrieveAsString(contentLength_));
    state_ = State::kGotAll;
    return Result::kComplete;
}

HttpRequest HttpParser::takeRequest() {
    HttpRequest result = std::move(request_);
    reset();
    return result;
}

void HttpParser::reset() {
    state_ = State::kRequestLine;
    contentLength_ = 0;
    parsedHeaderBytes_ = 0;
    request_ = HttpRequest();
}

}  // namespace myself
