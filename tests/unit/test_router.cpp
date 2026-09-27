#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "myself/http/HttpRequest.h"
#include "myself/http/HttpResponse.h"
#include "myself/http/Router.h"

namespace {

/// 临时目录：测试结束自动清理，避免污染 /tmp
class TempDir {
public:
    TempDir() {
        char pattern[] = "/tmp/myself_router_XXXXXX";
        char* created = ::mkdtemp(pattern);
        dir_ = created != nullptr ? std::string(created) : std::string();
    }

    ~TempDir() {
        if (dir_.empty()) {
            return;
        }
        std::remove((dir_ + "/index.html").c_str());
        ::rmdir(dir_.c_str());
    }

    const std::string& path() const { return dir_; }

private:
    std::string dir_;
};

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream out(path);
    out << content;
}

myself::HttpRequest makeGet(const std::string& path) {
    myself::HttpRequest request;
    request.setMethod(myself::HttpRequest::Method::kGet);
    request.setPath(path);
    return request;
}

}  // namespace

TEST(RouterTest, ServesExistingStaticFile) {
    TempDir dir;
    ASSERT_FALSE(dir.path().empty());
    writeFile(dir.path() + "/index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    const myself::HttpResponse response = router.handle(makeGet("/index.html"));

    EXPECT_EQ(200, response.statusCode());
    EXPECT_EQ("<h1>ok</h1>", response.body());

    const std::string raw = response.serialize();
    EXPECT_NE(std::string::npos, raw.find("Content-Type: text/html"));
    EXPECT_NE(std::string::npos, raw.find("Content-Length: 11"));
}

TEST(RouterTest, MapsRootToIndexHtml) {
    TempDir dir;
    writeFile(dir.path() + "/index.html", "root");

    myself::Router router(dir.path());
    EXPECT_EQ(200, router.handle(makeGet("/")).statusCode());
}

TEST(RouterTest, Returns404ForMissingFile) {
    TempDir dir;
    myself::Router router(dir.path());

    EXPECT_EQ(404, router.handle(makeGet("/nope.html")).statusCode());
}

TEST(RouterTest, RejectsPathTraversal) {
    TempDir dir;
    myself::Router router(dir.path());

    EXPECT_EQ(400, router.handle(makeGet("/../etc/passwd")).statusCode());
}

TEST(RouterTest, RejectsUnsupportedMethod) {
    TempDir dir;
    myself::Router router(dir.path());

    myself::HttpRequest request;
    request.setMethod(myself::HttpRequest::Method::kPost);
    request.setPath("/index.html");

    EXPECT_EQ(405, router.handle(request).statusCode());
}

TEST(RouterTest, RegisteredHandlerTakesPrecedence) {
    TempDir dir;
    myself::Router router(dir.path());
    router.registerHandler("/api/ping", [](const myself::HttpRequest&) {
        myself::HttpResponse response;
        response.setStatus(200, "OK");
        response.setContentType("application/json; charset=utf-8");
        response.setBody("{\"pong\":true}");
        return response;
    });

    const myself::HttpResponse response = router.handle(makeGet("/api/ping"));
    EXPECT_EQ(200, response.statusCode());
    EXPECT_EQ("{\"pong\":true}", response.body());
}

TEST(RouterTest, ContentTypeForCommonExtensions) {
    EXPECT_EQ("text/html; charset=utf-8", myself::Router::contentTypeFor("/a/index.html"));
    EXPECT_EQ("text/css; charset=utf-8", myself::Router::contentTypeFor("/a/style.css"));
    EXPECT_EQ("application/javascript; charset=utf-8",
              myself::Router::contentTypeFor("/a/app.js"));
    EXPECT_EQ("application/json; charset=utf-8", myself::Router::contentTypeFor("/a/b.json"));
    EXPECT_EQ("image/png", myself::Router::contentTypeFor("/a/x.png"));
    EXPECT_EQ("application/octet-stream", myself::Router::contentTypeFor("/a/x.bin"));
}
