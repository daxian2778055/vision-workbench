// 用 HDevEngine 执行官方 DLDataset 流程的最小训练脚本，
// 验证替换版 HALCON 的 train_dl_model_batch 是否可用（绕开 C++ 头文件裁剪）
#include "HDevEngineCpp.h"
#include <cstdio>
#include <string>

using namespace HDevEngineCpp;

int main(int argc, char **argv)
{
    const char *script = (argc > 1) ? argv[1] : "hde_min_train.hdev";
    try {
        HDevEngine engine;
        HDevProgram program(script);
        HDevProgramCall call(program);
        call.Execute();
        std::printf("脚本执行完成（无异常）\n");
    } catch (const HDevEngineException &e) {
        std::printf("脚本异常: %s (HALCON error %d, 行 %d)\n", e.Message(),
                    e.HalconErrorCode(), e.ProgLineNum());
        return 1;
    } catch (...) {
        std::printf("未知异常\n");
        return 1;
    }
    return 0;
}
