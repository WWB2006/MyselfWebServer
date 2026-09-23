#include "myself/http/Router.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace myself {

namespace {

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

bool Router::isPathSafe(const std::string& path) {
    // 拒绝目录穿越与空路径
    if (path.empty() || path[0] != '/') {
        return false;
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

std::string Router::contentTypeFor(const std::string& path) {
    const std::string lower = toLower(path);
    if (endsWith(lower, ".html") || endsWith(lower, ".htm")) {
        return "text/html; charset=utf-8";
    }
    if (endsWith(lower, ".css")) {
        return "text/css; charset=utf-8";
    }
    if (endsWith(lower, ".js")) {
        return "application/javascript; charset=utf-8";
    }
    if (endsWith(lower, ".json")) {
        return "application/json; charset=utf-8";
    }
    if (endsWith(lower, ".png")) {
        return "image/png";
    }
    if (endsWith(lower, ".jpg") || endsWith(lower, ".jpeg")) {
        return "image/jpeg";
    }
    if (endsWith(lower, ".svg")) {
        return "image/svg+xml";
    }
    if (endsWith(lower, ".ico")) {
        return "image/x-icon";
    }
    if (endsWith(lower, ".txt")) {
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}

HttpResponse Router::handle(const HttpRequest& request) const {
    if (request.method() != HttpRequest::Method::kGet &&
        request.method() != HttpRequest::Method::kHead) {
        HttpResponse response(true);
        response.setStatus(405, "Method Not Allowed");
        response.setContentType("text/html; charset=utf-8");
        response.setBody("<html><body><h1>405 Method Not Allowed</h1></body></html>");
        return response;
    }

    if (!isPathSafe(request.path())) {
        return HttpResponse::badRequest("Bad Request");
    }

    const auto it = handlers_.find(request.path());
    if (it != handlers_.end()) {
        return it->second(request);
    }

    std::string path = request.path();
    if (path == "/") {
        path = "/index.html";  // 默认首页
    }
    return serveStaticFile(path);
}

HttpResponse Router::serveStaticFile(const std::string& path) const {
    const std::string fullPath = wwwRoot_ + path;

    std::ifstream file(fullPath, std::ios::binary);
    if (!file) {
        return HttpResponse::notFound();
    }

    std::ostringstream content;
    content << file.rdbuf();

    HttpResponse response;
    response.setStatus(200, "OK");
    response.setContentType(contentTypeFor(path));
    response.setBody(content.str());
    return response;
}

}  // namespace myself
