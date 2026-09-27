# 单元测试：优先使用系统安装的 GoogleTest（离线可用），找不到时再联网下载
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS *.cpp)

add_executable(unit_tests ${TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE myself_core GTest::gtest_main)
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

include(GoogleTest)
gtest_discover_tests(unit_tests)
