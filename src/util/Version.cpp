#include "myself/util/Version.h"

// 版本号的唯一来源。
// 骨架阶段（阶段 0）为 0.1.0，随后按 CHANGELOG 逐阶段递增到 1.0.0；
// 这里固定为交付版本，需要按阶段区分时用编译期宏覆盖：
//     cmake -B build -DCMAKE_CXX_FLAGS='-DMYSELF_VERSION="0.4.0"'
// 或者直接给单个源文件加 -DMYSELF_VERSION=\"0.4.0\"。
#ifndef MYSELF_VERSION
#define MYSELF_VERSION "1.0.0"
#endif

namespace myself {

const char* version() {
    return MYSELF_VERSION;
}

std::string buildInfo() {
    return std::string("MyselfWebServer ") + version() + " (C++17 / epoll ET reactor)";
}

}  // namespace myself
