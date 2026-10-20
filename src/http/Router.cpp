#include "myself/http/Router.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

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

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(" \t");
    return text.substr(begin, end - begin + 1);
}

/// time_t -> UTC 的 tm。gmtime 返回静态缓冲区，多线程下不安全，因此用 *_r / *_s。
std::tm utcTm(std::time_t seconds) {
    std::tm result{};
#if defined(_WIN32)
    gmtime_s(&result, &seconds);
#else
    gmtime_r(&seconds, &result);
#endif
    return result;
}

/// RFC 9110 的 IMF-fixdate，例如 "Sun, 06 Nov 1994 08:49:37 GMT"
std::string formatHttpDate(std::time_t seconds) {
    const std::tm tmValue = utcTm(seconds);
    char buffer[64] = {0};
    std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &tmValue);
    return std::string(buffer);
}

/// 解析客户端回传的日期，兼容 RFC 9110 推荐的三种格式：
///   IMF-fixdate  "Sun, 06 Nov 1994 08:49:37 GMT"
///   RFC 850      "Sunday, 06-Nov-94 08:49:37 GMT"
///   asctime      "Sun Nov  6 08:49:37 1994"
/// 不用 strptime：它依赖 locale，且不是所有平台都提供。
bool parseHttpDate(const std::string& text, std::time_t* out) {
    std::string value = trim(text);
    // 丢掉开头的星期部分（"Sun," / "Sunday,"）
    const std::size_t comma = value.find(',');
    if (comma != std::string::npos) {
        value = trim(value.substr(comma + 1));
    }

    // 按空格与 '-' 切成词，时间字段自己带冒号
    std::vector<std::string> tokens;
    std::string current;
    for (char c : value) {
        if (c == ' ' || c == '\t' || c == '-') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }

    static const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = 0;
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    bool hasTime = false;

    for (const std::string& token : tokens) {
        if (token.find(':') != std::string::npos) {
            if (std::sscanf(token.c_str(), "%d:%d:%d", &hour, &minute, &second) != 3) {
                return false;
            }
            hasTime = true;
            continue;
        }
        if (token == "GMT" || token == "UTC") {
            continue;
        }
        bool numeric = !token.empty();
        for (char c : token) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                numeric = false;
                break;
            }
        }
        if (!numeric) {
            for (int i = 0; i < 12; ++i) {
                if (token == kMonths[i]) {
                    month = i + 1;
                    break;
                }
            }
            continue;
        }
        const int number = std::atoi(token.c_str());
        // 两位年份（RFC 850）按 RFC 9110 的规定映射到 19xx
        if (number > 31 || token.size() == 4 || number >= 70) {
            year = number;
        } else if (day == 0) {
            day = number;
        } else {
            year = number;
        }
    }

    if (!hasTime || month == 0 || day == 0 || year == 0) {
        return false;
    }
    if (year < 100) {
        year += (year < 70) ? 2000 : 1900;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
        second > 60) {
        return false;
    }

    // 公历日期 -> 1970-01-01 起的天数（Howard Hinnant 的 days_from_civil）
    const int adjustedYear = year - (month <= 2 ? 1 : 0);
    const long long era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
    const unsigned dayOfYear =
        (153u * static_cast<unsigned>(month > 2 ? month - 3 : month + 9) + 2u) / 5u +
        static_cast<unsigned>(day) - 1u;
    const unsigned dayOfEra =
        yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    const long long days = era * 146097LL + static_cast<long long>(dayOfEra) - 719468LL;

    *out = static_cast<std::time_t>(days * 86400LL + hour * 3600LL + minute * 60LL + second);
    return true;
}

/// 强校验器：修改时间 + 文件大小。任一变化都会让指纹变化，且不依赖文件内容哈希，
/// 代价只有一次 stat(2)，不会为了算 ETag 把整个文件读一遍。
std::string makeETag(std::time_t mtime, long long size) {
    char buffer[64] = {0};
    std::snprintf(buffer, sizeof(buffer), "\"%llx-%llx\"",
                  static_cast<unsigned long long>(mtime),
                  static_cast<unsigned long long>(size));
    return std::string(buffer);
}

/// If-None-Match 可能是逗号分隔的列表、可能带 W/ 弱前缀、也可能是 "*"
bool etagMatches(const std::string& headerValue, const std::string& etag) {
    if (trim(headerValue) == "*") {
        return true;
    }
    std::size_t begin = 0;
    while (begin <= headerValue.size()) {
        const std::size_t comma = headerValue.find(',', begin);
        const std::size_t length =
            (comma == std::string::npos) ? std::string::npos : comma - begin;
        std::string item = trim(headerValue.substr(begin, length));
        if (item.size() > 2 && item[0] == 'W' && item[1] == '/') {
            item = item.substr(2);  // 弱比较：忽略 W/ 前缀
        }
        if (!item.empty() && item == etag) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1;
    }
    return false;
}

/// 条件请求裁决：命中则返回 true（应答 304），调用方不必再读文件内容。
bool isNotModified(const HttpRequest& request, const std::string& etag, std::time_t mtime) {
    const std::string ifNoneMatch = request.getHeader("If-None-Match");
    if (!ifNoneMatch.empty()) {
        // RFC 9110 §13.1.3：只要带了 If-None-Match，就忽略 If-Modified-Since
        return etagMatches(ifNoneMatch, etag);
    }
    const std::string ifModifiedSince = request.getHeader("If-Modified-Since");
    if (!ifModifiedSince.empty()) {
        std::time_t since = 0;
        if (parseHttpDate(ifModifiedSince, &since)) {
            return mtime <= since;  // 秒级精度：文件不比客户端手里的副本更新
        }
    }
    return false;
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
    return serveStaticFile(path, request);
}

HttpResponse Router::serveStaticFile(const std::string& path,
                                     const HttpRequest& request) const {
    const std::string fullPath = wwwRoot_ + path;

    // 先用 stat 拿元数据：既判断存在性，也顺带拿到算 ETag 需要的 mtime 与大小。
    // 用 S_ISREG 挡掉目录，避免把目录当空文件返回 200。
    struct stat info {};
    if (::stat(fullPath.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
        return HttpResponse::notFound();
    }

    const std::time_t mtime = info.st_mtime;
    const std::string etag = makeETag(mtime, static_cast<long long>(info.st_size));
    const std::string lastModified = formatHttpDate(mtime);

    if (isNotModified(request, etag, mtime)) {
        // 304 只回校验器，零 Body：省掉磁盘读取与整包传输
        HttpResponse response;
        response.setStatus(304, "Not Modified");
        response.setHeader("ETag", etag);
        response.setHeader("Last-Modified", lastModified);
        response.setHeader("Cache-Control", "no-cache");
        return response;
    }

    // 大文件走零拷贝：不把内容读进用户态，交给 sendfile(2) 在内核里直接对拷
    if (zeroCopyThreshold_ > 0 &&
        static_cast<size_t>(info.st_size) >= zeroCopyThreshold_) {
        return serveFileBody(fullPath, path, static_cast<off_t>(info.st_size), etag,
                             lastModified);
    }

    // 按 stat 拿到的长度一次性分配再 read，而不是 ostringstream << rdbuf()：
    // libstdc++ 的 operator<<(streambuf*) 是 1KB 小块 sgetn 循环 + 末尾再整包 str() 拷贝，
    // 200MB 文件实测只有 60MB/s 量级；一次性 read 只要一次分配 + 一次系统调用。
    std::string body(static_cast<size_t>(info.st_size), '\0');
    if (!body.empty()) {
        std::ifstream file(fullPath, std::ios::binary);
        if (!file) {
            return HttpResponse::notFound();
        }
        file.read(&body[0], static_cast<std::streamsize>(body.size()));
        const std::streamsize got = file.gcount();
        if (got < static_cast<std::streamsize>(body.size())) {
            // 读取途中文件被截断：按实际读到的长度回，避免多吐垃圾字节
            body.resize(got > 0 ? static_cast<size_t>(got) : 0u);
        }
    }

    HttpResponse response;
    response.setStatus(200, "OK");
    response.setContentType(contentTypeFor(path));
    response.setHeader("ETag", etag);
    response.setHeader("Last-Modified", lastModified);
    response.setHeader("Cache-Control", "no-cache");
    response.setBody(std::move(body));
    return response;
}

HttpResponse Router::serveFileBody(const std::string& fullPath, const std::string& path,
                                   off_t size, const std::string& etag,
                                   const std::string& lastModified) const {
    // O_CLOEXEC：避免 fork 出去的子进程继承这个 fd
    const int fd = ::open(fullPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return HttpResponse::notFound();
    }

    HttpResponse response;
    response.setStatus(200, "OK");
    response.setContentType(contentTypeFor(path));
    // 文件体不在 body_ 里，必须显式给出长度，否则 serialize 会写成 0
    response.setHeader("Content-Length", std::to_string(static_cast<long long>(size)));
    response.setHeader("ETag", etag);
    response.setHeader("Last-Modified", lastModified);
    response.setHeader("Cache-Control", "no-cache");
    response.setFileBody(std::make_shared<FileBody>(fd, 0, static_cast<size_t>(size)));
    return response;
}

}  // namespace myself
