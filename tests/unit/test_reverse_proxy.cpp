#include <gtest/gtest.h>

#include "myself/http/HttpRequest.h"
#include "myself/http/HttpResponse.h"
#include "myself/http/ReverseProxy.h"
#include "myself/http/Router.h"

namespace {

myself::HttpRequest makeRequest(myself::HttpRequest::Method method, const std::string& path) {
    myself::HttpRequest req;
    req.setMethod(method);
    req.setPath(path);
    return req;
}

}  // namespace

TEST(ReverseProxyTest, RewritePathStripsPrefixAndKeepsQuery) {
    myself::ProxyOptions options;
    options.prefix = "/api/gateway";
    options.rewritePrefix = true;
    myself::ReverseProxy proxy(options);

    EXPECT_EQ("/api/data", proxy.rewritePath("/api/gateway/api/data", ""));
    EXPECT_EQ("/api/data?device=1", proxy.rewritePath("/api/gateway/api/data", "device=1"));
    EXPECT_EQ("/", proxy.rewritePath("/api/gateway", ""));
    EXPECT_EQ("/?foo=bar", proxy.rewritePath("/api/gateway", "foo=bar"));
    EXPECT_EQ("/health", proxy.rewritePath("/api/gateway/health", ""));
}

TEST(ReverseProxyTest, BadGatewayWhenUpstreamDead) {
    myself::ProxyOptions options;
    options.prefix = "/api/gateway";
    options.targetHost = "127.0.0.1";
    options.targetPort = 19999;  // 假设 19999 未被监听
    options.timeoutSeconds = 0.5;
    myself::ReverseProxy proxy(options);

    myself::HttpRequest req = makeRequest(myself::HttpRequest::Method::kGet, "/api/gateway/health");
    myself::HttpResponse resp = proxy.forward(req, "127.0.0.1");

    EXPECT_EQ(502, resp.statusCode());
    EXPECT_NE(std::string::npos, resp.body().find("bad_gateway"));
}

TEST(ReverseProxyTest, RouterPrefixHandlerTakesPrecedence) {
    myself::Router router("www");
    bool handlerCalled = false;
    std::string capturedClientIp;
    std::string capturedPath;

    router.registerPrefixHandler(
        "/api/gateway",
        [&](const myself::HttpRequest& req, const std::string& clientIp) {
            handlerCalled = true;
            capturedClientIp = clientIp;
            capturedPath = req.path();
            myself::HttpResponse resp;
            resp.setStatus(200, "OK");
            resp.setBody("{\"proxied\":true}");
            return resp;
        });

    myself::HttpRequest req = makeRequest(myself::HttpRequest::Method::kGet, "/api/gateway/devices");
    myself::HttpResponse resp = router.handle(req, "192.168.1.100");

    EXPECT_TRUE(handlerCalled);
    EXPECT_EQ("192.168.1.100", capturedClientIp);
    EXPECT_EQ("/api/gateway/devices", capturedPath);
    EXPECT_EQ(200, resp.statusCode());
    EXPECT_EQ("{\"proxied\":true}", resp.body());
}

TEST(ReverseProxyTest, RouterAllowsPostOnPrefixHandler) {
    myself::Router router("www");
    router.registerPrefixHandler(
        "/api/gateway",
        [](const myself::HttpRequest& req, const std::string&) {
            myself::HttpResponse resp;
            resp.setStatus(200, "OK");
            resp.setBody("echo:" + req.body());
            return resp;
        });

    myself::HttpRequest req;
    req.setMethod(myself::HttpRequest::Method::kPost);
    req.setPath("/api/gateway/control");
    req.setBody("start");

    // 静态文件不允许 POST，但前缀路由允许 POST
    myself::HttpResponse resp = router.handle(req, "127.0.0.1");
    EXPECT_EQ(200, resp.statusCode());
    EXPECT_EQ("echo:start", resp.body());
}
