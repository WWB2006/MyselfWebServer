#pragma once

#include <map>
#include <string>

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
    void setContentType(const std::string& type) { setHeader("Content-Type", type); }
    void setBody(std::string body) { body_ = std::move(body); }
    const std::string& body() const { return body_; }

    void setCloseConnection(bool on) { closeConnection_ = on; }
    bool closeConnection() const { return closeConnection_; }

    /// 序列化为可直接写入 socket 的字节串。
    std::string serialize(bool includeBody = true) const;

    static HttpResponse makeError(int code, const std::string& message);
    static HttpResponse notFound();
    static HttpResponse badRequest(const std::string& reason);
    static HttpResponse payloadTooLarge();

private:
    int statusCode_{200};
    std::string statusMessage_{"OK"};
    std::map<std::string, std::string> headers_;
    std::string body_;
    bool closeConnection_{false};
};

}  // namespace myself
