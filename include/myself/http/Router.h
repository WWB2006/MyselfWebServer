#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>

#include "myself/http/HttpRequest.h"
#include "myself/http/HttpResponse.h"

namespace myself {

/// 路由：把路径映射到处理函数，未命中的路径按静态文件处理。
class Router {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit Router(std::string wwwRoot = "www") : wwwRoot_(std::move(wwwRoot)) {}

    /// 注册接口，例如 "/api/status"。
    void registerHandler(const std::string& path, Handler handler) {
        handlers_[path] = std::move(handler);
    }

    HttpResponse handle(const HttpRequest& request) const;

    /// 按扩展名返回 Content-Type，未知类型回退为 text/plain。
    static std::string contentTypeFor(const std::string& path);

private:
    HttpResponse serveStaticFile(const std::string& path) const;
    static bool isPathSafe(const std::string& path);

    std::string wwwRoot_;
    std::map<std::string, Handler> handlers_;
};

}  // namespace myself
