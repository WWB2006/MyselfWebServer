#pragma once

#include <map>
#include <string>
#include <utility>

namespace myself {

/// 一条已解析完成的 HTTP 请求。
class HttpRequest {
public:
    enum class Method { kInvalid, kGet, kPost, kHead };

    Method method() const { return method_; }
    void setMethod(Method method) { method_ = method; }
    static Method methodFromString(const std::string& text);
    static const char* methodToString(Method method);

    const std::string& path() const { return path_; }
    const std::string& query() const { return query_; }
    const std::string& version() const { return version_; }
    const std::string& body() const { return body_; }

    /// 传入 "path?query" 形式，内部拆成 path 与 query。
    void setPath(const std::string& target);
    void setVersion(std::string version) { version_ = std::move(version); }
    void setBody(std::string body) { body_ = std::move(body); }

    /// header 键统一按小写保存，查询时大小写不敏感。
    void addHeader(const std::string& key, const std::string& value);
    bool hasHeader(const std::string& key) const;
    std::string getHeader(const std::string& key) const;
    const std::map<std::string, std::string>& headers() const { return headers_; }

    /// HTTP/1.1 默认长连接，连接头为 close 时关闭；HTTP/1.0 反之。
    bool isKeepAlive() const { return keepAlive_; }
    void setKeepAlive(bool on) { keepAlive_ = on; }

private:
    Method method_{Method::kInvalid};
    std::string path_{"/"};
    std::string query_;
    std::string version_{"HTTP/1.0"};
    std::string body_;
    std::map<std::string, std::string> headers_;
    bool keepAlive_{false};
};

}  // namespace myself
