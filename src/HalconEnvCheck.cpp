#include "HalconEnvCheck.h"
#include <halconcpp/HalconCpp.h>

using namespace HalconCpp;

namespace {

bool runImageLayerProbes(QString *errText)
{
    HImage img;
    GenImageConst(&img, "byte", 64, 64);

    HTuple width, height;
    GetImageSize(img, &width, &height);
    if (width.I() != 64 || height.I() != 64) {
        if (errText)
            *errText = QStringLiteral("图像层异常: GenImageConst 尺寸 = %1×%2（期望 64×64）")
                           .arg(width.I())
                           .arg(height.I());
        return false;
    }

    HTuple pointer, type, ptrWidth, ptrHeight;
    GetImagePointer1(img, &pointer, &type, &ptrWidth, &ptrHeight);
    if (pointer.L() == 0) {
        if (errText)
            *errText = QStringLiteral("图像层异常: GetImagePointer1 指针为空");
        return false;
    }
    return true;
}

QString okDetail()
{
    return QStringLiteral("HALCON 图像层可用（读图/显示）。\n"
                          "本软件日常算法走 OpenCV；HALCON 算法仅 DeepOCR。");
}

QString failDetail(const QString &err)
{
    return QStringLiteral("%1\n\n读取图像、显示、DeepOCR 会受影响。"
                          "二值化/匹配/卡尺等算法不依赖本次检测。")
        .arg(err);
}

} // namespace

HalconEnvStatus halconEnvironmentCheck(QString *detail)
{
    try {
        QString err;
        if (runImageLayerProbes(&err)) {
            if (detail)
                *detail = okDetail();
            return HalconEnvStatus::Ok;
        }
        if (detail)
            *detail = failDetail(err);
        return HalconEnvStatus::Fatal;
    } catch (const HException &e) {
        if (detail)
            *detail = failDetail(QStringLiteral("HALCON 基础调用异常: %1")
                                     .arg(QString::fromLocal8Bit(e.ErrorMessage().TextA())));
        return HalconEnvStatus::Fatal;
    } catch (...) {
        if (detail)
            *detail = failDetail(QStringLiteral("HALCON 未知异常"));
        return HalconEnvStatus::Fatal;
    }
}

HalconEnvStatus halconRuntimeProbe(QString *detail)
{
    return halconEnvironmentCheck(detail);
}
