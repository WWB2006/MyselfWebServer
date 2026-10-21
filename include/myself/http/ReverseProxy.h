#pragma once

#include <cstdint>
#include <string>

#include "myself/http/HttpRequest.h"
#include "myself/http/HttpResponse.h"

namespace myself {

struct ProxyOptions {
    std::string prefix{"/api/gateway"};
    std::string targetHost{"127.0.0.1"};
    uint16_t targetPort{8081};
    double timeoutSeconds{3.0};
    bool rewritePrefix{true};
    bool enabled{true};
};

/// 微型反向代理网关：将匹配 prefix 的外部请求转发到下游微服务（如 Modbus 采集网关 8081 端口），
/// 并将下游响应透传回客户端。在下游不可达时执行熔断防护，返回标准 502 Bad Gateway。
class ReverseProxy {
public:
    explicit ReverseProxy(ProxyOptions options = {});

    bool isEnabled() const { return options_.enabled; }
    void setEnabled(bool enabled) { options_.enabled = enabled; }

    const std::string& prefix() const { return options_.prefix; }
    const std::string& targetHost() const { return options_.targetHost; }
    uint16_t targetPort() const { return options_.targetPort; }
    double timeoutSeconds() const { return options_.timeoutSeconds; }
    const ProxyOptions& options() const { return options_; }

    /// 将客户端请求代理转发给下游服务
    HttpResponse forward(const HttpRequest& request, const std::string& clientIp = "") const;

    /// 路径重写：根据 options_.rewritePrefix 将外部路径剥离 prefix 前缀
    std::string rewritePath(const std::string& originalPath, const std::string& query = "") const;

private:
    ProxyOptions options_;
};

}  // namespace myself
