#include "myself/util/Version.h"

namespace myself {

std::string version() {
    return "0.1.0";   // 版本号按你项目实际情况填
}

std::string buildInfo() {
    return "MyselfWebServer " + version();
}

}  // namespace myself
