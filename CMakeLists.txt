cmake_minimum_required(VERSION 3.16)
project(webserver LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -g)

# 阶段 4 起使用 std::thread，必须显式链接线程库，否则会报 undefined reference
find_package(Threads REQUIRED)

# 阶段 8：把业务代码编成静态库，可执行文件与单元测试分别链接
option(WEBSERVER_BUILD_TESTS "构建单元测试（优先用系统 GTest，缺失时联网下载）" OFF)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    src/net/*.cpp
    src/http/*.cpp
    src/timer/*.cpp
    src/thread/*.cpp
    src/log/*.cpp
    src/util/*.cpp
)

add_library(myself_core STATIC ${CORE_SOURCES})
target_include_directories(myself_core PUBLIC include)
target_link_libraries(myself_core PUBLIC Threads::Threads)

add_executable(webserver src/main.cpp)
target_link_libraries(webserver PRIVATE myself_core)

if(WEBSERVER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()

