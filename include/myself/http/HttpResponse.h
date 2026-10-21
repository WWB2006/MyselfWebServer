#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

#include "myself/util/FileBody.h"

namespace myself {

/// 一条待发送的 HTTP 响应。
class HttpResponse {
public:
    explicit HttpResponse(bool closeConnection = false)
        : closeConnection_(closeConnection) {}

    void setStatus(int code, std::string message) {
        statusCode_ = code;
        statusMessage_ = std::move(message);
    }
    int statusCode() const { return statusCode_; }

    void setHeader(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }
    /// 读取响应头（键大小写不敏感）；未设置时返回空串。
    std::string header(const std::string& key) const {
        const auto it = headers_.find(key);
        if (it != headers_.end()) {
            return it->second;
        }
        for (const auto& item : headers_) {
            if (item.first.size() == key.size() &&
                std::equal(item.first.begin(), item.first.end(), key.begin(),
                           [](char a, char b) {
                               return std::tolower(static_cast<unsigned char>(a)) ==
                                      std::tolower(static_cast<unsigned char>(b));
                           })) {
                return item.second;
            }
        }
        return std::string();
    }
    void setContentType(const std::string& type) { setHeader("Content-Type", type); }
    void setBody(std::string body) { body_ = std::move(body); }
    const std::string& body() const { return body_; }

    /// 设置零拷贝文件体：body_ 保持为空，由 TcpConnection 用 sendfile(2) 直接发送。
    /// 调用方必须显式设置 Content-Length（serialize 只按 body_ 长度兜底）。
    void setFileBody(FileBodyPtr file) { fileBody_ = std::move(file); }
    bool hasFileBody() const { return fileBody_ != nullptr; }
    const FileBodyPtr& fileBody() const { return fileBody_; }

    void setCloseConnection(bool on) { closeConnection_ = on; }
    bool closeConnection() const { return closeConnection_; }

    /// 序列化为可直接写入 socket 的字节串。
    std::string serialize(bool includeBody = true) const;

    static HttpResponse makeError(int code, const std::string& message);
    static HttpResponse notFound();
    static HttpResponse badRequest(const std::string& reason);
    static HttpResponse payloadTooLarge();
    /// 过载保护：连接数超过上限时返回，提示客户端稍后重试
    static HttpResponse serviceUnavailable();
    /// 限流保护：请求速率超过令牌桶上限时返回 429 Too Many Requests，并携带 Retry-After
    static HttpResponse tooManyRequests(int retryAfterSeconds = 1);
    /// 反向代理错误：下游网关不可达或连接拒绝
    static HttpResponse badGateway(const std::string& reason = "Bad Gateway");
    /// 反向代理超时：下游网关响应超时
    static HttpResponse gatewayTimeout(const std::string& reason = "Gateway Timeout");

private:
    int statusCode_{200};
    std::string statusMessage_{"OK"};
    std::map<std::string, std::string> headers_;
    std::string body_;
    FileBodyPtr fileBody_;
    bool closeConnection_{false};
};

}  // namespace myself
