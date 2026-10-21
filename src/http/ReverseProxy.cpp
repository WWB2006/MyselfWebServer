#include "myself/http/ReverseProxy.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

#include "myself/log/Logger.h"

namespace myself {

ReverseProxy::ReverseProxy(ProxyOptions options) : options_(std::move(options)) {}

std::string ReverseProxy::rewritePath(const std::string& originalPath,
                                      const std::string& query) const {
    std::string path = originalPath;
    if (options_.rewritePrefix && !options_.prefix.empty()) {
        if (path.rfind(options_.prefix, 0) == 0) {
            path = path.substr(options_.prefix.size());
            if (path.empty() || path[0] != '/') {
                path = "/" + path;
            }
        }
    }
    if (path.empty()) {
        path = "/";
    }
    if (!query.empty()) {
        path += "?" + query;
    }
    return path;
}

HttpResponse ReverseProxy::forward(const HttpRequest& request,
                                   const std::string& clientIp) const {
    if (!options_.enabled) {
        return HttpResponse::badGateway("Reverse proxy is disabled");
    }

    // 1. 创建 TCP 套接字
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR << "reverse proxy socket() failed: " << std::strerror(errno);
        return HttpResponse::badGateway("Failed to create socket: " +
                                        std::string(std::strerror(errno)));
    }

    // 2. 设置连接与读写超时
    struct timeval tv {};
    tv.tv_sec = static_cast<time_t>(options_.timeoutSeconds);
    tv.tv_usec = static_cast<suseconds_t>(
        (options_.timeoutSeconds - static_cast<double>(tv.tv_sec)) * 1000000.0);
    if (tv.tv_sec <= 0 && tv.tv_usec <= 0) {
        tv.tv_sec = 3;
    }
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // 3. 解析目标地址
    struct sockaddr_in servAddr {};
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(options_.targetPort);
    if (::inet_pton(AF_INET, options_.targetHost.c_str(), &servAddr.sin_addr) <= 0) {
        struct addrinfo hints {};
        struct addrinfo* res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (::getaddrinfo(options_.targetHost.c_str(), nullptr, &hints, &res) != 0 ||
            res == nullptr) {
            ::close(fd);
            LOG_ERROR << "reverse proxy resolve host failed: " << options_.targetHost;
            return HttpResponse::badGateway("Failed to resolve upstream host: " +
                                            options_.targetHost);
        }
        auto* ipv4 = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
        servAddr.sin_addr = ipv4->sin_addr;
        ::freeaddrinfo(res);
    }

    // 4. 连接下游
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&servAddr), sizeof(servAddr)) < 0) {
        const int err = errno;
        ::close(fd);
        LOG_WARN << "reverse proxy connect to " << options_.targetHost << ":"
                 << options_.targetPort << " failed: " << std::strerror(err);
        return HttpResponse::badGateway("Cannot reach upstream " + options_.targetHost + ":" +
                                        std::to_string(options_.targetPort) + ": " +
                                        std::strerror(err));
    }

    // 5. 构建发送给下游的 HTTP 报文
    const std::string targetUri = rewritePath(request.path(), request.query());
    const char* methodStr = HttpRequest::methodToString(request.method());

    std::ostringstream reqStream;
    reqStream << methodStr << " " << targetUri << " HTTP/1.1\r\n";
    reqStream << "Host: " << options_.targetHost << ":" << options_.targetPort << "\r\n";
    reqStream << "Connection: close\r\n";

    std::string clientHost;
    std::string forwardedFor;
    for (const auto& header : request.headers()) {
        const std::string& key = header.first;
        const std::string& val = header.second;
        if (key == "host") {
            clientHost = val;
            continue;
        }
        if (key == "connection" || key == "content-length") {
            continue;
        }
        if (key == "x-forwarded-for") {
            forwardedFor = val;
            continue;
        }
        reqStream << key << ": " << val << "\r\n";
    }

    // 注入反向代理链路跟踪头
    if (!clientIp.empty()) {
        if (!forwardedFor.empty()) {
            forwardedFor += ", " + clientIp;
        } else {
            forwardedFor = clientIp;
        }
        reqStream << "X-Forwarded-For: " << forwardedFor << "\r\n";
        reqStream << "X-Real-IP: " << clientIp << "\r\n";
    }
    if (!clientHost.empty()) {
        reqStream << "X-Forwarded-Host: " << clientHost << "\r\n";
    }
    reqStream << "X-Forwarded-Proto: http\r\n";

    const std::string& body = request.body();
    if (!body.empty()) {
        reqStream << "Content-Length: " << body.size() << "\r\n";
    }
    reqStream << "\r\n";
    if (!body.empty()) {
        reqStream << body;
    }

    const std::string rawRequest = reqStream.str();

    // 6. 发送数据
    size_t totalSent = 0;
    while (totalSent < rawRequest.size()) {
        const ssize_t n =
            ::write(fd, rawRequest.data() + totalSent, rawRequest.size() - totalSent);
        if (n <= 0) {
            const int err = errno;
            ::close(fd);
            LOG_ERROR << "reverse proxy write failed: " << std::strerror(err);
            return HttpResponse::badGateway("Write to upstream failed: " +
                                            std::string(std::strerror(err)));
        }
        totalSent += static_cast<size_t>(n);
    }

    // 7. 读取下游响应（因为请求了 Connection: close，直接读到 EOF 即可）
    std::string responseRaw;
    char buffer[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            responseRaw.append(buffer, static_cast<size_t>(n));
        } else if (n == 0) {
            break;  // 对端正常关闭写端（EOF）
        } else {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ::close(fd);
                return HttpResponse::gatewayTimeout("Timeout waiting for upstream response");
            }
            const int err = errno;
            ::close(fd);
            return HttpResponse::badGateway("Read from upstream failed: " +
                                            std::string(std::strerror(err)));
        }
    }
    ::close(fd);

    if (responseRaw.empty()) {
        return HttpResponse::badGateway("Empty response from upstream");
    }

    // 8. 解析下游响应
    const std::size_t headerEnd = responseRaw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        HttpResponse resp;
        resp.setStatus(200, "OK");
        resp.setContentType("text/plain; charset=utf-8");
        resp.setBody(responseRaw);
        resp.setHeader("X-Proxy-By", "MyselfWebServer");
        return resp;
    }

    const std::string headerPart = responseRaw.substr(0, headerEnd);
    const std::string bodyPart = responseRaw.substr(headerEnd + 4);

    std::istringstream stream(headerPart);
    std::string statusLine;
    if (!std::getline(stream, statusLine)) {
        return HttpResponse::badGateway("Malformed status line from upstream");
    }
    if (!statusLine.empty() && statusLine.back() == '\r') {
        statusLine.pop_back();
    }

    // 解析状态行：HTTP/1.1 200 OK
    int statusCode = 200;
    std::string statusMsg = "OK";
    const std::size_t firstSpace = statusLine.find(' ');
    if (firstSpace != std::string::npos) {
        const std::size_t secondSpace = statusLine.find(' ', firstSpace + 1);
        if (secondSpace != std::string::npos) {
            statusCode =
                std::atoi(statusLine.substr(firstSpace + 1, secondSpace - firstSpace - 1).c_str());
            statusMsg = statusLine.substr(secondSpace + 1);
        } else {
            statusCode = std::atoi(statusLine.substr(firstSpace + 1).c_str());
        }
    }

    HttpResponse response;
    response.setStatus(statusCode, statusMsg);

    std::string headerLine;
    while (std::getline(stream, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') {
            headerLine.pop_back();
        }
        if (headerLine.empty()) {
            continue;
        }
        const std::size_t colon = headerLine.find(':');
        if (colon != std::string::npos) {
            std::string key = headerLine.substr(0, colon);
            std::string val = headerLine.substr(colon + 1);
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
                val.erase(val.begin());
            }
            if (key != "Connection" && key != "connection") {
                response.setHeader(key, val);
            }
        }
    }

    response.setBody(bodyPart);
    response.setHeader("X-Proxy-By", "MyselfWebServer");
    response.setHeader("X-Upstream-Addr",
                       options_.targetHost + ":" + std::to_string(options_.targetPort));
    return response;
}

}  // namespace myself
