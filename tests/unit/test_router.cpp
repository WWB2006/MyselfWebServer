#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

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
        for (const std::string& name : files_) {
            std::remove((dir_ + "/" + name).c_str());
        }
        for (auto it = dirs_.rbegin(); it != dirs_.rend(); ++it) {
            ::rmdir((dir_ + "/" + *it).c_str());
        }
        ::rmdir(dir_.c_str());
    }

    const std::string& path() const { return dir_; }

    /// 写文件并登记，析构时一并删除
    void write(const std::string& name, const std::string& content) {
        std::ofstream out(dir_ + "/" + name);
        out << content;
        out.close();
        files_.push_back(name);
    }

    /// 建子目录并登记
    bool makeDir(const std::string& name) {
        if (::mkdir((dir_ + "/" + name).c_str(), 0755) != 0) {
            return false;
        }
        dirs_.push_back(name);
        return true;
    }

private:
    std::string dir_;
    std::vector<std::string> files_;
    std::vector<std::string> dirs_;
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

myself::HttpRequest makeGetWith(const std::string& path, const std::string& key,
                                const std::string& value) {
    myself::HttpRequest request = makeGet(path);
    request.addHeader(key, value);
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

// ---------- 条件请求：ETag / Last-Modified / 304 ----------

TEST(RouterTest, StaticFileCarriesCacheValidators) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    const myself::HttpResponse response = router.handle(makeGet("/index.html"));

    EXPECT_EQ(200, response.statusCode());

    const std::string etag = response.header("ETag");
    ASSERT_GE(etag.size(), 2u);
    EXPECT_EQ('"', etag.front());   // 强校验器必须带双引号
    EXPECT_EQ('"', etag.back());

    const std::string lastModified = response.header("Last-Modified");
    EXPECT_NE(std::string::npos, lastModified.find("GMT"));
    EXPECT_EQ("no-cache", response.header("Cache-Control"));
}

TEST(RouterTest, MatchingIfNoneMatchReturns304WithoutBody) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    const std::string etag = router.handle(makeGet("/index.html")).header("ETag");
    ASSERT_FALSE(etag.empty());

    const myself::HttpResponse response =
        router.handle(makeGetWith("/index.html", "If-None-Match", etag));

    EXPECT_EQ(304, response.statusCode());
    EXPECT_TRUE(response.body().empty());
    EXPECT_EQ(etag, response.header("ETag"));

    // 304 必须没有实体：序列化后以空行结尾，后面不跟任何字节
    const std::string raw = response.serialize();
    EXPECT_EQ("\r\n\r\n", raw.substr(raw.size() - 4));
}

TEST(RouterTest, WeakAndListedIfNoneMatchAlsoMatch) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    const std::string etag = router.handle(makeGet("/index.html")).header("ETag");
    ASSERT_FALSE(etag.empty());

    // 弱校验器前缀 W/ 参与弱比较
    EXPECT_EQ(304, router.handle(makeGetWith("/index.html", "If-None-Match", "W/" + etag))
                       .statusCode());
    // 逗号分隔列表里命中任意一项即算命中
    EXPECT_EQ(304, router.handle(makeGetWith("/index.html", "If-None-Match",
                                             "\"stale-etag\", " + etag))
                       .statusCode());
    // "*" 表示只要资源存在就算命中
    EXPECT_EQ(304, router
                       .handle(makeGetWith("/index.html", "If-None-Match", "*"))
                       .statusCode());
}

TEST(RouterTest, StaleIfNoneMatchReturns200) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    const myself::HttpResponse response =
        router.handle(makeGetWith("/index.html", "If-None-Match", "\"deadbeef-0\""));

    EXPECT_EQ(200, response.statusCode());
    EXPECT_EQ("<h1>ok</h1>", response.body());
}

TEST(RouterTest, IfModifiedSinceEqualToLastModifiedReturns304) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    const std::string lastModified =
        router.handle(makeGet("/index.html")).header("Last-Modified");
    ASSERT_FALSE(lastModified.empty());

    const myself::HttpResponse response =
        router.handle(makeGetWith("/index.html", "If-Modified-Since", lastModified));

    EXPECT_EQ(304, response.statusCode());
    EXPECT_TRUE(response.body().empty());
}

TEST(RouterTest, OlderIfModifiedSinceReturns200) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    // 客户端副本比文件更旧 -> 必须回完整实体
    const myself::HttpResponse response = router.handle(
        makeGetWith("/index.html", "If-Modified-Since", "Sun, 06 Nov 1994 08:49:37 GMT"));

    EXPECT_EQ(200, response.statusCode());
    EXPECT_EQ("<h1>ok</h1>", response.body());
}

TEST(RouterTest, IfNoneMatchTakesPrecedenceOverIfModifiedSince) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    const std::string etag = router.handle(makeGet("/index.html")).header("ETag");
    const std::string lastModified =
        router.handle(makeGet("/index.html")).header("Last-Modified");
    ASSERT_FALSE(etag.empty());
    ASSERT_FALSE(lastModified.empty());

    // ETag 不匹配 + IMS 看起来很新：按 RFC 9110，以 If-None-Match 为准 -> 200
    myself::HttpRequest request = makeGet("/index.html");
    request.addHeader("If-None-Match", "\"stale-etag\"");
    request.addHeader("If-Modified-Since", lastModified);
    EXPECT_EQ(200, router.handle(request).statusCode());

    // ETag 匹配 + IMS 很旧：同样以 If-None-Match 为准 -> 304
    myself::HttpRequest second = makeGet("/index.html");
    second.addHeader("If-None-Match", etag);
    second.addHeader("If-Modified-Since", "Sun, 06 Nov 1994 08:49:37 GMT");
    EXPECT_EQ(304, router.handle(second).statusCode());
}

TEST(RouterTest, ETagChangesWhenFileContentChanges) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    const std::string before = router.handle(makeGet("/index.html")).header("ETag");
    ASSERT_FALSE(before.empty());

    // 内容变长：即使 mtime 落在同一秒，指纹也必须变化
    dir.write("index.html", "<h1>ok</h1><p>more</p>");
    const std::string after = router.handle(makeGet("/index.html")).header("ETag");

    EXPECT_FALSE(after.empty());
    EXPECT_NE(before, after);
    // 旧指纹失效：拿旧 ETag 请求必须回完整实体，而不是错误的 304
    EXPECT_EQ(200, router.handle(makeGetWith("/index.html", "If-None-Match", before))
                       .statusCode());
}

TEST(RouterTest, DirectoryIsNotServedAsFile) {
    TempDir dir;
    ASSERT_TRUE(dir.makeDir("assets"));

    myself::Router router(dir.path());
    EXPECT_EQ(404, router.handle(makeGet("/assets")).statusCode());
}

// ---------- 零拷贝：大文件只回头部 + 文件区间 ----------

TEST(RouterTest, LargeFileUsesZeroCopyFileBody) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    router.setZeroCopyThreshold(1);  // 让 11 字节的文件也走零拷贝分支

    const myself::HttpResponse response = router.handle(makeGet("/index.html"));

    EXPECT_EQ(200, response.statusCode());
    EXPECT_TRUE(response.hasFileBody());
    EXPECT_TRUE(response.body().empty());  // 内容没有读进用户态
    EXPECT_EQ("11", response.header("Content-Length"));

    // 从 fd 读回内容，确认文件区间正确
    ASSERT_TRUE(response.hasFileBody());
    char buffer[64] = {0};
    const ssize_t n = ::pread(response.fileBody()->fd(), buffer, sizeof(buffer),
                              response.fileBody()->offset());
    ASSERT_EQ(11, n);
    EXPECT_EQ("<h1>ok</h1>", std::string(buffer, static_cast<size_t>(n)));
}

TEST(RouterTest, ZeroCopyThresholdZeroKeepsBodyInMemory) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    router.setZeroCopyThreshold(0);  // 关闭零拷贝，便于做 A/B 压测

    const myself::HttpResponse response = router.handle(makeGet("/index.html"));

    EXPECT_EQ(200, response.statusCode());
    EXPECT_FALSE(response.hasFileBody());
    EXPECT_EQ("<h1>ok</h1>", response.body());
}

TEST(RouterTest, ZeroCopyConditionalRequestReturns304WithoutFileBody) {
    TempDir dir;
    dir.write("index.html", "<h1>ok</h1>");

    myself::Router router(dir.path());
    router.setZeroCopyThreshold(1);

    const std::string etag = router.handle(makeGet("/index.html")).header("ETag");
    ASSERT_FALSE(etag.empty());

    const myself::HttpResponse response =
        router.handle(makeGetWith("/index.html", "If-None-Match", etag));

    EXPECT_EQ(304, response.statusCode());
    // 304 不该把文件 fd 带出来，否则每条 304 都白开一次 fd
    EXPECT_FALSE(response.hasFileBody());
    EXPECT_TRUE(response.body().empty());
}
