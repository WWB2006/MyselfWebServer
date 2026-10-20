#pragma once

#include <sys/types.h>

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>

#include "myself/http/HttpRequest.h"
#include "myself/http/HttpResponse.h"

namespace myself {

/// 路由：把路径映射到处理函数，未命中的路径按静态文件处理。
/// 静态文件响应带强 ETag 与 Last-Modified，并处理条件请求（命中返回 304）。
class Router {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit Router(std::string wwwRoot = "www") : wwwRoot_(std::move(wwwRoot)) {}

    /// 注册接口，例如 "/api/status"。
    void registerHandler(const std::string& path, Handler handler) {
        handlers_[path] = std::move(handler);
    }

    HttpResponse handle(const HttpRequest& request) const;

    /// 大于等于该字节数的静态文件走 sendfile(2) 零拷贝；0 表示一律读进内存。
    /// 小文件走内存是有意的：为几 KB 的数据多开一次 fd、多一次 sendfile 系统调用并不划算。
    void setZeroCopyThreshold(size_t bytes) { zeroCopyThreshold_ = bytes; }
    size_t zeroCopyThreshold() const { return zeroCopyThreshold_; }

    /// 按扩展名返回 Content-Type，未知类型回退为 text/plain。
    static std::string contentTypeFor(const std::string& path);

private:
    /// 读取静态文件；request 只用于取 If-None-Match / If-Modified-Since
    HttpResponse serveStaticFile(const std::string& path, const HttpRequest& request) const;
    /// 零拷贝分支：只回头部 + 文件区间，不把内容读进用户态
    HttpResponse serveFileBody(const std::string& fullPath, const std::string& path,
                               off_t size, const std::string& etag,
                               const std::string& lastModified) const;
    static bool isPathSafe(const std::string& path);

    std::string wwwRoot_;
    size_t zeroCopyThreshold_{64 * 1024};
    std::map<std::string, Handler> handlers_;
};

}  // namespace myself
