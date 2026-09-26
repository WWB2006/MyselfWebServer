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

# 阶段 4：主从 Reactor 与线程池 —— 放置说明

## 一、文件放置位置

| 本目录文件 | 仓库中的位置 | 说明 |
| --- | --- | --- |
| include/myself/net/EventLoop.h | 同路径覆盖 | 增加跨线程投递与唤醒通道 |
| src/net/EventLoop.cpp | 同路径覆盖 | 用 eventfd 唤醒，去掉临时调试打印 |
| include/myself/net/EventLoopThread.h | 新增 | 一个线程 + 一个事件循环 |
| src/net/EventLoopThread.cpp | 新增 | 线程内创建并运行 EventLoop |
| include/myself/net/EventLoopThreadPool.h | 新增 | 主从 Reactor 的“从”部分 |
| src/net/EventLoopThreadPool.cpp | 新增 | 轮询分配连接 |
| include/myself/thread/ThreadPool.h | 新增 | 通用计算线程池 |
| src/thread/ThreadPool.cpp | 新增 | 任务队列 + 条件变量 |
| include/myself/net/TcpConnection.h | 同路径覆盖 | 只增加一个 `loop()` 访问器，用于退出时回到正确线程关闭连接 |
| src/main.cpp | 同路径覆盖 | 主线程 accept，IO 线程读写，worker 线程处理耗时任务 |
| CMakeLists.txt | 同路径覆盖 | 加入新源文件、去重 Version.cpp、链接 Threads |
| docs/adr/0003-main-sub-reactor.md | docs/adr/ | 决策记录 |
| docs/stage-4-验收记录.md | docs/ | 环境、命令、压测对比表 |

## 二、构建与运行

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

./build/webserver -t 0 -p 8080          # 单线程基线（阶段 3 行为）
./build/webserver -t 4 -p 8080          # 4 个 IO 线程
./build/webserver -t 4 -w 4 -p 8080     # 再加 4 个 worker 线程
```

功能验证：

```bash
curl -i http://127.0.0.1:8080/index.html
curl -s http://127.0.0.1:8080/api/status      # 返回 ioThreads / workerThreads
grep Threads /proc/$(pgrep webserver)/status  # 线程数核对
```

观察连接是否分散到多个 IO 线程：

```bash
for i in 1 2 3 4 5 6; do (nc 127.0.0.1 8080 &) ; done
# 服务端日志里应出现 -> io-0、io-1、io-2、io-3 中的多个
```

## 三、压测对比（阶段 4 的核心产出）

压测客户端一定要放在宿主机或另一台机器上，和服务器挤在同一台虚拟机会互相抢 CPU，
数据没有说服力。

```bash
# 基线
./build/webserver -t 0 -p 8080
wrk -t4 -c200 -d60s --latency http://<虚拟机IP>:8080/index.html

# 多 IO 线程
./build/webserver -t 4 -p 8080
wrk -t4 -c200 -d60s --latency http://<虚拟机IP>:8080/index.html

# 多 IO 线程 + worker 池
./build/webserver -t 4 -w 4 -p 8080
wrk -t4 -c200 -d60s --latency http://<虚拟机IP>:8080/index.html
```

把三组数据填进 `docs/stage-4-验收记录.md` 的压测表，并写一句结论（提升多少、瓶颈在哪）。

## 四、这一版代码的关键点

1. **连接绑定 IO 线程**：连接对象在它所属的 IO 线程里创建、注册、读写与销毁；
   跨线程注册事件会命中 `assertInLoopThread()` 直接终止，这是有意为之。
2. **eventfd 唤醒**：`runInLoop` 从其他线程投递任务时写 8 字节，让 `epoll_wait` 立刻返回；
   否则任务要等到下一次事件才执行。
3. **线程归属断言**：`updateChannel` / `removeChannel` 只允许在循环线程调用，
   把“偶发崩溃”变成“立即暴露”。
4. **worker 线程不碰 socket**：worker 只做文件读取与响应构造，写回通过
   `ioLoop->runInLoop` 回到 IO 线程，这是多线程服务器最容易写错的地方。
5. **连接表加锁**：accept 在主线程插入、close 在 IO 线程删除，必须用互斥量保护。
6. **CMake 必须链接 Threads**：漏掉会报 `undefined reference to pthread_create`。
7. **退出顺序**：先 `registry.closeAll()` 把每条连接交回它所属的 IO 线程关闭，再回收线程池；
   否则连接对象会在主线程析构，析构里调用 `disableAll()` 命中线程归属断言直接终止进程。
8. **去掉了调试打印**：你原来的 `EventLoop.cpp` 里有 `[loop] waiting...`、`[dispatch]` 等临时输出，
   这一版换成了按需的 `EventLoop::name()` 日志；需要重新排查时用线程名定位即可。
9. **CMake 顺手清理**：原来的源文件列表里 `src/util/Version.cpp` 出现了三次，这一版去重了。

## 五、本机无法编译，已做的人工自查

我这边只有 Windows 工具链（没有 epoll/eventfd 头文件），所以**没有编译验证**，下面是逐项核对过的内容：

| 检查项 | 结论 |
| --- | --- |
| 新增文件是否都写进 CMakeLists | 是，EventLoopThread、EventLoopThreadPool、ThreadPool 均已加入 |
| 是否链接线程库 | 是，`find_package(Threads)` + `Threads::Threads` |
| 头文件自洽 | EventLoop 增加 `<utility>`，EventLoopThreadPool.cpp 增加 `<utility>`，TcpConnection.h 增加 `<utility>` |
| 跨线程访问 | 连接表加锁；socket 只在所属 IO 线程读写；worker 线程只做文件读取与响应构造 |
| 退出路径 | registry.closeAll() → 各连接回到自己的 IO 线程关闭 → 线程池析构 quit + join |
| 生命周期 | 连接创建、注册、关闭都通过 `runInLoop` 保证在同一线程；`Channel::tie` 保证回调期间对象存活 |

如果编译报错，把完整报错贴给我；常见的第一批问题是：忘了把新文件加进 CMakeLists、
漏链接 Threads、或者把 `send()` 直接写在了 worker 线程里。

## 六、验收标准

1. `-t 0` 与阶段 3 行为一致，`-t 4` 时连接分散到多个 IO 线程；
2. `curl` 的 200 / 404 / `/api/status` 全部正常；
3. 三组压测数据齐全，结论能解释瓶颈；
4. ThreadSanitizer 或 helgrind 跑一轮无数据竞争报告；
5. 压测结束后没有残留连接与 fd 泄漏；
6. 能回答：为什么连接必须绑定固定的 IO 线程、eventfd 解决什么问题、worker 线程为什么不能直接 `send`。

## 七、提交

```bash
git checkout -b stage4-threadpool
git add CMakeLists.txt include src
git commit -m "feat: 阶段4 主从 Reactor 与线程池"
git add docs/adr/0003-main-sub-reactor.md docs/stage-4-验收记录.md
git commit -m "docs: 阶段4 决策记录与验收记录"
git push -u origin stage4-threadpool
```

## 八、README 需要同步的改动

把阶段进度表里的阶段 4 一行改成：

```
| 4 | 线程池与主从 Reactor | 已完成 |
```

并在“设计决策”一节追加：

```
- [ADR 0003：主从 Reactor 与线程池](docs/adr/0003-main-sub-reactor.md)
```

# 阶段 5：定时器与空闲连接超时 —— 放置说明

## 一、文件放置位置

| 本目录文件 | 仓库中的位置 | 说明 |
| --- | --- | --- |
| include/myself/util/Timestamp.h | 新增 | 单调时钟封装与毫秒换算（header-only） |
| include/myself/timer/Timer.h | 新增 | TimerId 句柄与 Timer 定义 |
| src/timer/Timer.cpp | 新增 | 序号自增与重复定时器重排 |
| include/myself/timer/TimerQueue.h | 新增 | 定时器队列（只在 loop 线程访问内部状态） |
| src/timer/TimerQueue.cpp | 新增 | 插入、取消、最近到期时间与到期处理 |
| include/myself/net/EventLoop.h | 同路径覆盖 | 增加 runAt/runAfter/runEvery/cancelTimer 与 TimerQueue 成员 |
| src/net/EventLoop.cpp | 同路径覆盖 | 用最近到期时间作为 epoll_wait 超时；每轮处理到期定时器 |
| include/myself/net/TcpConnection.h | 同路径覆盖 | 增加 setIdleTimeout 与空闲定时器成员 |
| src/net/TcpConnection.cpp | 同路径覆盖 | 空闲计时刷新、超时关闭、关闭时取消定时器（并去掉你的调试打印） |
| src/main.cpp | 同路径覆盖 | 新增 `-i` 参数、周期统计 runEvery、状态接口补充字段 |
| CMakeLists.txt | 同路径覆盖 | 加入 timer 模块两个源文件 |
| docs/adr/0004-timer-design.md | docs/adr/ | 决策记录 |
| docs/stage-5-验收记录.md | docs/ | 环境、功能验收、精度与吞吐对比 |

## 二、构建与运行

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"

./build/webserver -t 4 -p 8080            # 不启用空闲超时
./build/webserver -t 4 -i 5 -p 8080       # 空闲 5 秒断开
```

## 三、验证

```bash
# 1. 空闲超时：连上不输入，5 秒左右被服务端断开
nc 127.0.0.1 8080
# 服务端应打印：[timeout] fd=... idle over 5s, closing

# 2. 有数据往来不误杀：每 2 秒发一行，持续 20 秒
while true; do echo hello; sleep 2; done | nc 127.0.0.1 8080

# 3. 周期任务：每 10 秒打印一次统计
# [stats] online=..., timers=..., up=...s

# 4. 状态接口
curl -s http://127.0.0.1:8080/api/status

# 5. 定时器取消：短连接请求后立即断开，等待超时时间，不应出现该 fd 的 [timeout]
curl -s http://127.0.0.1:8080/index.html
```

## 四、这一版代码的关键点

1. **定时精度来自 epoll_wait 超时**：`EventLoop::loop` 每轮向 `TimerQueue` 要"最近还有多少毫秒"，
   没有定时器时返回 -1（一直阻塞），已经到期返回 0（立刻处理）。
2. **时间用单调时钟**：`steady_clock` 不受系统时间调整影响；日志里用相对启动的毫秒数，便于阅读。
3. **TimerQueue 不做内部加锁**：所有状态只在循环线程访问，跨线程调用通过 `runInLoop` 转发，
   并借助阶段 4 的 eventfd 立即唤醒循环。
4. **重复定时器先重排再执行**：这样回调里取消自己也能被正确移除，不会留下幽灵任务。
5. **TimerId 带 owner 指针**：拿别的队列的句柄来取消会被忽略，避免误删同序号的其他任务。
6. **空闲超时只持弱引用**：`weak_ptr` + 关闭时 `cancelTimer`，双保险避免回调访问已销毁的连接。
7. **统计用原子变量**：定时器任务在 main 线程更新计数，`/api/status` 可能在任意 IO 线程读取，
   所以用 `std::atomic<size_t>` 而不是普通变量。
8. **去掉了调试打印**：`TcpConnection.cpp` 里原来的 `[handleRead] fd=... called` 已删除。

## 五、验收标准

1. `-i 5` 时空闲连接在 5 秒左右被断开，日志有 `[timeout]`；
2. 有数据往来时不会被误杀，主动断开立即关闭；
3. 关闭超时功能（不带 `-i`）时行为与阶段 4 一致；
4. `/api/status` 返回 `idleTimeoutSeconds` 与 `timers` 字段；
5. 每 10 秒输出一次 `[stats]`，说明 `runEvery` 可用；
6. 压测数据与阶段 4 基线对比，QPS 下降在可解释范围内；
7. 能回答：为什么用 epoll_wait 超时而不是 timerfd、TimerQueue 为什么不加锁、
   重复定时器为什么先重排再执行。

## 六、提交

```bash
git checkout -b stage5-timer
git add CMakeLists.txt include src
git commit -m "feat: 阶段5 定时器与空闲连接超时"
git add docs/adr/0004-timer-design.md docs/stage-5-验收记录.md
git commit -m "docs: 阶段5 决策记录与验收记录"
git push -u origin stage5-timer
```

## 七、README 需要同步的改动

阶段进度表：

```
| 5 | 定时器与空闲连接超时 | 已完成 |
```

设计决策一节追加：

```
- [ADR 0004：定时器与空闲连接超时](docs/adr/0004-timer-design.md)
```

# 阶段 6：日志系统 —— 放置说明

## 一、文件放置位置

| 本目录文件 | 仓库中的位置 | 说明 |
| --- | --- | --- |
| include/myself/log/LogStream.h、src/log/LogStream.cpp | 新增 | 栈上固定缓冲与类型输出，零分配 |
| include/myself/log/Logger.h、src/log/Logger.cpp | 新增 | 级别、时间戳、线程 id、LOG_* 宏 |
| include/myself/log/LogFile.h、src/log/LogFile.cpp | 新增 | 追加写文件，按大小与日期轮转 |
| include/myself/log/AsyncLogging.h、src/log/AsyncLogging.cpp | 新增 | 后台线程 + 双缓冲，批量落盘 |
| src/main.cpp | 同路径覆盖 | 日志初始化、`-l/-g/-s/-q` 参数、全部输出改为 LOG_* |
| src/net/EventLoop.cpp | 同路径覆盖 | 线程归属断言改为 LOG_ERROR |
| src/net/Epoller.cpp | 同路径覆盖 | 3 处错误改为 LOG_ERROR，去掉 add OK 调试打印 |
| src/net/Acceptor.cpp | 同路径覆盖 | 监听与 accept 失败改为 LOG_INFO/LOG_ERROR |
| src/net/EventLoopThreadPool.cpp | 同路径覆盖 | 线程池启动改为 LOG_INFO |
| src/net/TcpConnection.cpp | 同路径覆盖 | 超时/读写错误改为 LOG_*，删掉 3 行 [start] 调试打印 |
| src/thread/ThreadPool.cpp | 同路径覆盖 | 启动与任务异常改为 LOG_* |
| CMakeLists.txt | 同路径覆盖 | 加入 log 模块 4 个源文件 |
| docs/adr/0005-logging-design.md | docs/adr/ | 决策记录 |
| docs/stage-6-验收记录.md | docs/ | 级别过滤、轮转、QPS 影响对比 |

## 二、构建与运行

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"

./build/webserver -t 4 -p 8080                          # 默认 info 级别，只输出控制台
./build/webserver -t 4 -l warn -p 8080                  # 只保留告警与错误
./build/webserver -t 4 -l info -g logs -s 32 -p 8080    # 异步落盘，按 32MB 轮转
./build/webserver -t 4 -l info -g logs -q -p 8080       # 静默：只写文件
```

## 三、验证

```bash
# 1. 日志格式（时间 线程id 级别 内容 文件:行号）
./build/webserver -t 2 -p 8080
# 20260926 20:15:03.123456 [INFO ] acceptor listening on port 8080 - Acceptor.cpp:33

# 2. 级别过滤
./build/webserver -l warn -p 8080     # 正常请求不再打印
./build/webserver -l debug -p 8080    # 能看到 epoll add 这类 DEBUG

# 3. 异步落盘与轮转
./build/webserver -g logs -s 1 -p 8080 &
ls -lh logs/                          # 出现 server.<时间>.<pid>.log
wrk -t4 -c200 -d60s http://127.0.0.1:8080/index.html
ls logs/ | wc -l                      # 超过 1MB 后文件数量增加

# 4. 静默模式对比 QPS
./build/webserver -l info -g logs -q -p 8080
wrk -t4 -c200 -d60s --latency http://127.0.0.1:8080/index.html
```

## 四、这一版代码的关键点

1. **前端/后端分离**：`Logger` 只格式化，输出目标由函数指针决定，因此控制台、异步文件、
   测试用内存缓冲可以互换。
2. **零分配格式化**：`FixedBuffer` 在栈上，整数用除法逐位写出，避免每条日志一次 `std::string`。
3. **宏里先判级别**：`if (Logger::level() <= kInfo)` 保证低于阈值的日志连拼接都不发生。
4. **异步双缓冲**：前端写满一块就交给后端，后端批量写文件并归还缓冲，避免频繁 write 与锁竞争。
5. **按大小与日期轮转**：单文件超过 `-s` 指定大小立刻换文件，同时保证每天至少一个文件。
6. **时间基准分开**：日志用 `CLOCK_REALTIME`（人能对上时间），定时器用 `steady_clock`（单调）。
7. **清理调试打印**：删掉了 `TcpConnection::start()` 里的 3 行 `[start]` 与 `Epoller::add` 的
   `add OK` 打印，改为 DEBUG 级日志。

## 五、验收标准

1. 四种级别过滤都生效，格式统一且带线程 id；
2. `-g` 时日志写入文件，`-s 1` 能观察到轮转；
3. `-q` 时控制台无输出但文件仍在写；
4. 压测数据完整：warn/info/debug 与控制台开关四组对比；
5. 多线程下日志不交错、不丢行；
6. 能回答：为什么要前后端分离、异步日志丢日志的窗口有多大、宏里先判级别省掉了什么。

## 六、提交

```bash
git checkout -b stage6-logging
git add CMakeLists.txt include src
git commit -m "feat: 阶段6 分级日志与异步落盘"
git add docs/adr/0005-logging-design.md docs/stage-6-验收记录.md
git commit -m "docs: 阶段6 决策记录与验收记录"
git push -u origin stage6-logging
```

## 七、README 需要同步的改动

阶段进度表：

```
| 6 | 日志系统 | 已完成 |
```

设计决策一节追加：

```
- [ADR 0005：日志系统的前后端分离与异步落盘](docs/adr/0005-logging-design.md)
```

