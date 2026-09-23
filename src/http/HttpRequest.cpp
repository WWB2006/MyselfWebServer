#include "myself/http/HttpRequest.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace myself {
namespace {

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(" \t");
    return text.substr(begin, end - begin + 1);
}

}  // namespace

HttpRequest::Method HttpRequest::methodFromString(const std::string& text) {
    if (text == "GET") {
        return Method::kGet;
    }
    if (text == "POST") {
        return Method::kPost;
    }
    if (text == "HEAD") {
        return Method::kHead;
    }
    return Method::kInvalid;
}

const char* HttpRequest::methodToString(Method method) {
    switch (method) {
        case Method::kGet:
            return "GET";
        case Method::kPost:
            return "POST";
        case Method::kHead:
            return "HEAD";
        default:
            return "INVALID";
    }
}

void HttpRequest::setPath(const std::string& target) {
    const auto pos = target.find('?');
    if (pos == std::string::npos) {
        path_ = target;
        query_.clear();
    } else {
        path_ = target.substr(0, pos);
        query_ = target.substr(pos + 1);
    }
    if (path_.empty()) {
        path_ = "/";
    }
}

void HttpRequest::addHeader(const std::string& key, const std::string& value) {
    headers_[toLower(trim(key))] = trim(value);
}

bool HttpRequest::hasHeader(const std::string& key) const {
    return headers_.find(toLower(key)) != headers_.end();
}

std::string HttpRequest::getHeader(const std::string& key) const {
    const auto it = headers_.find(toLower(key));
    return it == headers_.end() ? std::string() : it->second;
}

}  // namespace myself
