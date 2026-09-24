cmake_minimum_required(VERSION 3.16)
project(webserver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -g)

# 阶段 4 起使用 std::thread，必须显式链接线程库，否则会报 undefined reference
find_package(Threads REQUIRED)

add_executable(webserver
    src/main.cpp

    src/net/Socket.cpp
    src/net/Acceptor.cpp
    src/net/Buffer.cpp
    src/net/Channel.cpp
    src/net/Epoller.cpp
    src/net/EventLoop.cpp
    src/net/EventLoopThread.cpp
    src/net/EventLoopThreadPool.cpp
    src/net/TcpConnection.cpp

    src/thread/ThreadPool.cpp

    src/http/HttpParser.cpp
    src/http/HttpRequest.cpp
    src/http/HttpResponse.cpp
    src/http/Router.cpp

    src/util/Version.cpp
)

target_include_directories(webserver PRIVATE include)
target_link_libraries(webserver PRIVATE Threads::Threads)

