// 环境自检模块验证：直接调用 halconEnvironmentCheck 并打印结果
#include "HalconEnvCheck.h"
#include <cstdio>

int main()
{
    QString detail;
    const HalconEnvStatus s = halconEnvironmentCheck(&detail);
    std::printf("status=%d (%s)\n", static_cast<int>(s),
                s == HalconEnvStatus::Ok ? "Ok" :
                s == HalconEnvStatus::RegionBroken ? "RegionBroken" : "Fatal");
    std::printf("detail: %s\n", detail.toUtf8().constData());
    return 0;
}
