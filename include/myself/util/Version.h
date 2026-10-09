#pragma once

#include <string>

namespace myself {

/// 当前版本号。
/// 与 CHANGELOG.md 的 [1.0.0] 条目对应；如需在构建时覆盖，
/// 传入 -DMYSELF_VERSION=\"x.y.z\" 即可（见 src/util/Version.cpp）。
const char* version();

/// 供启动日志使用的一行描述，例如 "MyselfWebServer 1.0.0 (C++17 / epoll ET reactor)"。
std::string buildInfo();

}  // namespace myself
