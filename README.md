# MyselfWebServer

基于 epoll 的 C++17 高并发网络服务器，用于系统学习 Linux 网络编程与服务器工程化实践。

项目从最小可运行的工程骨架开始，按阶段逐步实现阻塞模型、非阻塞 Reactor、HTTP 协议解析、
线程池、定时器与日志系统，每个阶段都保留可运行版本与设计决策记录。

## 当前状态

**阶段 0 已完成**：工程骨架可构建、可运行，具备统一的目录结构、构建脚本、编码规范与文档体系。
下一步进入阶段 1（阻塞式 TCP 回显服务）。

## 快速开始

依赖：Ubuntu 22.04 及以上（或任意带 GCC 11 与 CMake 3.16 的 Linux 发行版）。

```bash
# 1. 构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 2. 编译
cmake --build build -j"$(nproc)"

# 3. 运行
./build/bin/MyselfWebServer -p 8080
```

期望输出：

```
webserver 0.1.0 (stage 0 skeleton) starting, listen port = 8080
stage 0 done: next step is the blocking TCP echo server
```

### 启用单元测试

单元测试使用 GoogleTest，通过 CMake 的 FetchContent 拉取，需要联网：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DWEBSERVER_BUILD_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

网络不便时改用系统包：`sudo apt install -y libgtest-dev`，并把 `tests/unit/CMakeLists.txt`
里的 FetchContent 段替换为 `find_package(GTest REQUIRED)`。

### 常用脚本

| 脚本 | 用途 |
| --- | --- |
| `scripts/build.sh` | 一条命令构建（可用 `BUILD_TYPE=Release` 覆盖构建类型） |
| `scripts/run.sh` | 按 `PORT` 环境变量启动服务 |
| `scripts/bench.sh` | 压测并把环境与结果写入 `docs/bench/` |
| `scripts/sanitize.sh asan\|tsan` | 带消毒器的构建，用于排查内存与竞态问题 |

## 目录结构

| 目录 | 职责 |
| --- | --- |
| `include/myself/` | 对外头文件，按模块分目录（net、http、timer、thread、log、util） |
| `src/` | 实现文件，与 `include` 结构保持一致；`main.cpp` 只做装配 |
| `tests/unit/` | GoogleTest 单元测试 |
| `tests/api/` | pytest 接口测试（阶段 3 之后补齐） |
| `scripts/` | 构建、运行、压测与消毒器脚本 |
| `docs/adr/` | 关键设计决策记录，一次决定一页 |
| `docs/bench/` | 压测原始数据与汇总 |
| `www/` | 静态资源 |
| `config/` | 运行配置 |

依赖方向是单向的：`util` 不依赖任何模块，`net` 依赖 `util` 与 `log`，
`http` 依赖 `net`，`main.cpp` 负责装配全部模块。详细约定见 ADR 0002。

## 阶段进度

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| 0 | 工程骨架与 CMake | 已完成 |
| 1 | 阻塞式 TCP 回显服务 | 进行中 |
| 2 | 非阻塞 epoll 事件循环 | 待开始 |
| 3 | HTTP 解析、静态文件与路由 | 待开始 |
| 4 | 线程池与主从 Reactor | 待开始 |
| 5 | 定时器与空闲连接超时 | 待开始 |
| 6 | 日志系统 | 待开始 |
| 7 | 运行指标与接口扩展 | 待开始 |
| 8 | 测试体系与持续集成 | 待开始 |
| 9 | 性能优化与稳定性验证 | 待开始 |

## 设计决策

- [ADR 0001：使用 CMake 与 C++17](docs/adr/0001-cmake-and-cpp17.md)
- [ADR 0002：目录分层与依赖方向](docs/adr/0002-layered-directories.md)

## 数据与结论

压测数据、火焰图与优化对比记录在 `docs/bench/` 与对应的 ADR 中，
所有数字均来自本机实测，并标注测试环境与完整命令。

## 开发环境

| 项 | 版本 |
| --- | --- |
| 操作系统 | Ubuntu 22.04 LTS |
| 编译器 | GCC 11（C++17） |
| 构建工具 | CMake 3.22、Make 或 Ninja |
| 调试工具 | GDB、AddressSanitizer、Valgrind |
| 压测工具 | wrk |

## 来源与许可

学习过程中参考了开源项目 TinyWebServer 与 muduo 的设计思路，核心模块自行实现。
参考项目的许可证在其上游仓库中保留说明，使用前请自行核对。

本项目采用 MIT 许可证（如与参考项目代码有衍生关系，请按上游许可证要求调整）。

## 联系

GitHub：<https://github.com/WWB2006/MyselfWebServer>

# MyselfWebServer 阶段 0 改名与阶段 1 落地

本目录提供两样东西：把已生成的骨架改名为 MyselfWebServer 的命令，以及阶段 1 的三个源文件。

## 一、把骨架改名为 MyselfWebServer

命名约定：

| 项 | 取值 |
| --- | --- |
| 仓库与目录名 | MyselfWebServer |
| CMake project 名 | MyselfWebServer |
| 可执行文件名 | MyselfWebServer（产物在 build/bin/） |
| 核心静态库 | myself_core |
| 命名空间 | myself |
| 头文件目录 | include/myself/ |

在项目上一级目录执行：

```bash
cd ~/code
mv webserver MyselfWebServer
cd MyselfWebServer

# 1. 头文件目录改名
mv include/webserver include/myself

# 2. 修正 include 路径
grep -rl 'webserver/' --include='*.cpp' --include='*.h' . \
  | xargs sed -i 's|webserver/|myself/|g'

# 3. 修正命名空间与版本常量
grep -rl 'webserver' --include='*.cpp' --include='*.h' . \
  | xargs sed -i 's/namespace webserver/namespace myself/g; s/webserver::/myself::/g'

# 4. 修正 CMake 目标名与项目名
sed -i 's/webserver_core/myself_core/g' CMakeLists.txt src/CMakeLists.txt tests/unit/CMakeLists.txt
sed -i 's/project(webserver /project(MyselfWebServer /' CMakeLists.txt
sed -i 's/add_executable(webserver /add_executable(MyselfWebServer /' src/CMakeLists.txt

# 5. 修正脚本与编辑器配置里的产物路径
grep -rl 'bin/webserver' --include='*.sh' --include='*.json' . \
  | xargs sed -i 's|bin/webserver|bin/MyselfWebServer|g'

# 6. 验证旧名字已经清理干净
grep -rn 'webserver_core\|add_executable(webserver\|namespace webserver' . \
  --include='*.txt' --include='*.cpp' --include='*.h' || echo "改名完成"

# 7. 重新构建
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
./build/bin/MyselfWebServer -p 8080
```

README.md 的标题改成 MyselfWebServer，然后提交：

```bash
git add -A
git commit -m "refactor: rename project to MyselfWebServer"
```

## 二、阶段 1：阻塞式 TCP 回显服务

把本目录的文件放到工程对应位置（覆盖原有文件）：

| 本目录文件 | 目标位置 |
| --- | --- |
| include/myself/net/Socket.h | include/myself/net/Socket.h |
| src/net/Socket.cpp | src/net/Socket.cpp |
| src/main.cpp | src/main.cpp |

另外把 include/myself/util/Version.h、src/util/Version.cpp 里的命名空间改成 myself（如果第 3 步已经做过就跳过）。

构建与验证：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
./build/bin/MyselfWebServer -p 8080

# 终端 2：正常回显
nc 127.0.0.1 8080
hello
# 屏幕应回显 hello，按 Ctrl+C 退出
```

故意暴露的缺陷（写进 ADR，阶段 2 解决）：保持第一个 nc 连接不断开，再开一个终端执行 nc，第二个连接虽然能建立，
但不会收到任何回显，因为服务端还阻塞在第一个连接里。

提交：

```bash
git add -A
git commit -m "feat: add blocking tcp echo server"
```

## 三、阶段 1 的验收标准

1. 服务能启动并打印监听信息；
2. nc 发送的内容会原样回显；
3. 客户端断开后服务端继续处理下一个连接；
4. 重启服务不会报 Address already in use（SO_REUSEADDR 生效）；
5. 能解释“为什么单连接阻塞模型在第二个客户端上失效”，以及阶段 2 会怎么改。

# MyselfWebServer 阶段 0 改名与阶段 1 落地

本目录提供两样东西：把已生成的骨架改名为 MyselfWebServer 的命令，以及阶段 1 的三个源文件。

## 一、把骨架改名为 MyselfWebServer

命名约定：

| 项 | 取值 |
| --- | --- |
| 仓库与目录名 | MyselfWebServer |
| CMake project 名 | MyselfWebServer |
| 可执行文件名 | MyselfWebServer（产物在 build/bin/） |
| 核心静态库 | myself_core |
| 命名空间 | myself |
| 头文件目录 | include/myself/ |

在项目上一级目录执行：

```bash
cd ~/code
mv webserver MyselfWebServer
cd MyselfWebServer

# 1. 头文件目录改名
mv include/webserver include/myself

# 2. 修正 include 路径
grep -rl 'webserver/' --include='*.cpp' --include='*.h' . \
  | xargs sed -i 's|webserver/|myself/|g'

# 3. 修正命名空间与版本常量
grep -rl 'webserver' --include='*.cpp' --include='*.h' . \
  | xargs sed -i 's/namespace webserver/namespace myself/g; s/webserver::/myself::/g'

# 4. 修正 CMake 目标名与项目名
sed -i 's/webserver_core/myself_core/g' CMakeLists.txt src/CMakeLists.txt tests/unit/CMakeLists.txt
sed -i 's/project(webserver /project(MyselfWebServer /' CMakeLists.txt
sed -i 's/add_executable(webserver /add_executable(MyselfWebServer /' src/CMakeLists.txt

# 5. 修正脚本与编辑器配置里的产物路径
grep -rl 'bin/webserver' --include='*.sh' --include='*.json' . \
  | xargs sed -i 's|bin/webserver|bin/MyselfWebServer|g'

# 6. 验证旧名字已经清理干净
grep -rn 'webserver_core\|add_executable(webserver\|namespace webserver' . \
  --include='*.txt' --include='*.cpp' --include='*.h' || echo "改名完成"

# 7. 重新构建
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
./build/bin/MyselfWebServer -p 8080
```

README.md 的标题改成 MyselfWebServer，然后提交：

```bash
git add -A
git commit -m "refactor: rename project to MyselfWebServer"
```

## 二、阶段 1：阻塞式 TCP 回显服务

把本目录的文件放到工程对应位置（覆盖原有文件）：

| 本目录文件 | 目标位置 |
| --- | --- |
| include/myself/net/Socket.h | include/myself/net/Socket.h |
| src/net/Socket.cpp | src/net/Socket.cpp |
| src/main.cpp | src/main.cpp |

另外把 include/myself/util/Version.h、src/util/Version.cpp 里的命名空间改成 myself（如果第 3 步已经做过就跳过）。

构建与验证：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
./build/bin/MyselfWebServer -p 8080

# 终端 2：正常回显
nc 127.0.0.1 8080
hello
# 屏幕应回显 hello，按 Ctrl+C 退出
```

故意暴露的缺陷（写进 ADR，阶段 2 解决）：保持第一个 nc 连接不断开，再开一个终端执行 nc，第二个连接虽然能建立，
但不会收到任何回显，因为服务端还阻塞在第一个连接里。

提交：

```bash
git add -A
git commit -m "feat: add blocking tcp echo server"
```

## 三、阶段 1 的验收标准

1. 服务能启动并打印监听信息；
2. nc 发送的内容会原样回显；
3. 客户端断开后服务端继续处理下一个连接；
4. 重启服务不会报 Address already in use（SO_REUSEADDR 生效）；
5. 能解释“为什么单连接阻塞模型在第二个客户端上失效”，以及阶段 2 会怎么改。

# MyselfWebServer 阶段 2：非阻塞 epoll 单线程 Reactor

把阶段 1 的阻塞式单连接模型改成非阻塞 + epoll，一个线程同时服务多个连接。

## 一、文件放置位置

| 本目录文件 | 目标位置 |
| --- | --- |
| include/myself/net/Buffer.h | include/myself/net/Buffer.h |
| include/myself/net/Epoller.h | include/myself/net/Epoller.h |
| include/myself/net/Channel.h | include/myself/net/Channel.h |
| include/myself/net/EventLoop.h | include/myself/net/EventLoop.h |
| include/myself/net/TcpConnection.h | include/myself/net/TcpConnection.h |
| include/myself/net/Acceptor.h | include/myself/net/Acceptor.h |
| src/net/Buffer.cpp | src/net/Buffer.cpp |
| src/net/Epoller.cpp | src/net/Epoller.cpp |
| src/net/Channel.cpp | src/net/Channel.cpp |
| src/net/EventLoop.cpp | src/net/EventLoop.cpp |
| src/net/TcpConnection.cpp | src/net/TcpConnection.cpp |
| src/net/Acceptor.cpp | src/net/Acceptor.cpp |
| src/main.cpp | src/main.cpp（覆盖阶段 1 的版本） |

`include/myself/net/Socket.h` 与 `src/net/Socket.cpp` 沿用阶段 1 的文件，不需要改动。
新文件会被 `GLOB_RECURSE ... CONFIGURE_DEPENDS` 自动纳入构建，不用改 CMake。

## 二、构建与验证

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
./build/bin/MyselfWebServer -p 8080
```

终端 2、3、4 分别打开一个连接，交替输入内容：

```bash
nc 127.0.0.1 8080      # 终端 2
nc 127.0.0.1 8080      # 终端 3，阶段 1 会卡住，阶段 2 应该正常
nc 127.0.0.1 8080      # 终端 4
```

期望现象：三个连接都能独立回显、互不阻塞；服务端打印每个连接的 accept、echo 与 close 日志。

用 awk 直接做一次压力检查（可选）：

```bash
seq 1 200 | xargs -P 20 -I{} sh -c 'echo hello | nc -q1 127.0.0.1 8080 > /dev/null'
ss -tn state established | wc -l      # 连接应全部正常关闭，无残留
```

## 三、代码结构说明

| 文件 | 职责 | 关键点 |
| --- | --- | --- |
| Buffer | 线性缓冲区，读写指针 + 前移复用 | `readFd` 用 `readv` 一次读到 EAGAIN，避免多次系统调用 |
| Epoller | epoll 的增删改与等待 | 只封装系统调用，不含业务逻辑 |
| Channel | 一个 fd 关心的事件与回调 | `tie()` 锁住宿主生命周期，防止回调中对象被销毁 |
| EventLoop | 等待事件并分发到 Channel | fd 到 Channel 的映射，支持运行中增删 |
| TcpConnection | 一条连接的收发与关闭 | ET 模式循环读到 EAGAIN；写不完进输出缓冲并注册 EPOLLOUT |
| Acceptor | 监听与 accept | `accept4(..., SOCK_NONBLOCK)`，循环取到 EAGAIN |
| main.cpp | 装配：建循环、建 Acceptor、维护连接表 | 连接关闭时从连接表移除 |

## 四、必须理解的四个点

1. **ET 模式为什么要循环读**：一次事件只通知一次，如果不读到 EAGAIN，剩余数据要等下一次事件，可能永远等不到。
2. **写不完怎么办**：`write` 返回值小于请求长度时必须把剩余数据存进输出缓冲区并注册 `EPOLLOUT`，否则数据静默丢失。
3. **回调里对象被销毁的风险**：连接关闭回调会从连接表里删除自己，`Channel::tie()` 保证回调执行期间对象仍然存活。
4. **为什么先取消关注再关闭 fd**：先 `disableAll()` + `remove()`，再从 epoll 里注销、最后 close，避免事件循环继续分发已关闭的 fd。

## 五、验收标准

1. 三个及以上客户端并发连接，回显互不阻塞；
2. 客户端断开后服务端日志出现 close，连接表大小回到 0；
3. `ss -tn state established` 在压测结束后没有残留连接；
4. 用 `valgrind --leak-check=full ./build/bin/MyselfWebServer` 或 ASan 版本跑一轮无泄漏；
5. 能回答：ET 与 LT 的区别、为什么用 `accept4` 而不是 `accept`、`tie()` 解决什么问题。

## 六、提交

```bash
git add -A
git commit -m "feat: add non-blocking epoll reactor with multi-connection echo"
git push
```

# MyselfWebServer 阶段 3：HTTP 解析、静态文件与路由

在阶段 2 的 Reactor 之上接入 HTTP：请求解析（状态机）、响应构造、静态文件服务与简单路由。

## 一、文件放置位置

| 本目录文件 | 目标位置 |
| --- | --- |
| include/myself/http/HttpRequest.h | include/myself/http/HttpRequest.h |
| include/myself/http/HttpParser.h | include/myself/http/HttpParser.h |
| include/myself/http/HttpResponse.h | include/myself/http/HttpResponse.h |
| include/myself/http/Router.h | include/myself/http/Router.h |
| src/http/HttpRequest.cpp | src/http/HttpRequest.cpp |
| src/http/HttpParser.cpp | src/http/HttpParser.cpp |
| src/http/HttpResponse.cpp | src/http/HttpResponse.cpp |
| src/http/Router.cpp | src/http/Router.cpp |
| src/main.cpp | src/main.cpp（覆盖阶段 2 的版本） |

`www/index.html` 与 `www/404.html` 沿用阶段 0 生成的文件，可以直接被访问。

## 二、构建与验证

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
./build/bin/MyselfWebServer -p 8080 -r www
```

终端 2 逐条验证：

```bash
# 1. 静态文件 200，且响应头带 Content-Length
curl -i http://127.0.0.1:8080/index.html

# 2. 根路径默认映射到首页
curl -i http://127.0.0.1:8080/

# 3. 不存在的路径返回 404
curl -i http://127.0.0.1:8080/not-exist

# 4. 长连接：两次请求复用同一条 TCP 连接
curl -iv http://127.0.0.1:8080/index.html http://127.0.0.1:8080/index.html 2>&1 | grep -i "re-using\|connected"

# 5. 路由接口
curl -s http://127.0.0.1:8080/api/status

# 6. 目录穿越被拒绝
curl -i "http://127.0.0.1:8080/../etc/passwd"

# 7. 半包与粘包：先发一半请求头，再发剩下的
printf 'GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r' | nc 127.0.0.1 8080
# 观察服务端不会误判为错误，补齐后半段后返回 200
```

服务端日志应出现 `[http] GET /index.html -> 200`、`[http] GET /not-exist -> 404` 这样的记录。

## 三、代码结构说明

| 文件 | 职责 | 关键点 |
| --- | --- | --- |
| HttpRequest | 保存解析结果：方法、路径、查询串、版本、头部、请求体 | 头部键统一转小写，查询大小写不敏感 |
| HttpParser | 有限状态机：请求行 → 请求头 → 请求体 → 完成 | 只吃 Buffer，不碰 socket；半包返回 kNeedMore，粘包靠循环调用继续解析 |
| HttpResponse | 状态行、头部、响应体的序列化 | 自动补 `Content-Length`、`Date`、`Connection`；HEAD 请求只发头部 |
| Router | 路径到处理函数的映射，未命中按静态文件处理 | 默认路径映射到 index.html；拒绝 `..` 目录穿越；按扩展名给出 Content-Type |
| main.cpp | 装配：每条连接一个解析器，循环解析后交给路由 | 支持 pipelining；短连接在响应后 `shutdownWrite()` |

## 四、四个必须理解的点

1. **半包**：请求行或请求头没读完时返回 `kNeedMore`，状态机保留中间状态，下次可读事件继续解析。
2. **粘包 / pipelining**：一次 read 可能收到多条完整请求，所以解析用 `for (;;)` 循环，直到缓冲区里没有完整请求为止。
3. **Content-Length 决定请求体长度**：解析头部时记录长度，请求体不足时继续等待；超过上限直接返回 400/413。
4. **响应必须带 Content-Length**，否则浏览器或 curl 会一直等待连接结束。

## 五、验收标准

1. 浏览器打开 `http://虚拟机IP:8080/` 能看到首页；
2. `curl -i` 访问不存在的路径返回 404，且响应头有 Content-Length；
3. `/api/status` 返回 JSON；
4. 半包与 pipelining 场景都能正确解析，服务端日志与预期一致；
5. `curl` 连续两次访问复用同一条连接（长连接生效）；
6. 目录穿越请求被拒绝。

## 六、提交

```bash
git add include/myself/http src/http src/main.cpp
git commit -m "feat: add http parser, static file service and router"
git add docs/adr/0004-http状态机与半包处理.md
git commit -m "docs: add adr for http parser state machine"
git push
```


