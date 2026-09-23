#include "myself/http/HttpResponse.h"

#include <ctime>

namespace myself {

namespace {

std::string httpDate() {
    char buffer[64];
    const std::time_t now = std::time(nullptr);
    std::tm tmValue{};
    gmtime_r(&now, &tmValue);
    std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &tmValue);
    return buffer;
}

}  // namespace

std::string HttpResponse::serialize(bool includeBody) const {
    std::string result;
    result.reserve(body_.size() + 256);

    result += "HTTP/1.1 ";
    result += std::to_string(statusCode_);
    result += " ";
    result += statusMessage_;
    result += "\r\n";
    result += "Server: MyselfWebServer\r\n";
    result += "Date: " + httpDate() + "\r\n";

    for (const auto& item : headers_) {
        result += item.first;
        result += ": ";
        result += item.second;
        result += "\r\n";
    }

    if (headers_.find("Content-Length") == headers_.end()) {
        result += "Content-Length: " + std::to_string(body_.size()) + "\r\n";
    }
    result += closeConnection_ ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
    result += "\r\n";

    if (includeBody) {
        result += body_;
    }
    return result;
}

HttpResponse HttpResponse::makeError(int code, const std::string& message) {
    HttpResponse response(true);
    response.setStatus(code, message);
    response.setContentType("text/html; charset=utf-8");
    response.setBody("<html><body><h1>" + std::to_string(code) + " " + message +
                     "</h1></body></html>");
    return response;
}

HttpResponse HttpResponse::notFound() {
    return makeError(404, "Not Found");
}

HttpResponse HttpResponse::badRequest(const std::string& reason) {
    return makeError(400, reason.empty() ? "Bad Request" : reason);
}

HttpResponse HttpResponse::payloadTooLarge() {
    return makeError(413, "Payload Too Large");
}

}  // namespace myself
