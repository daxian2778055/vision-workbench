// 脚本节点的图像 I/O 契约（2026-10-27 补齐）：脚本以独立进程运行，输入图经
// --vfp-image-in=… 传入，产出图写到 --vfp-image-out=…。这两条腿证明契约真通：
//   ① 脚本把输入图原样回传 → 输出图与输入图逐像素一致（不依赖任何第三方库，只用 shutil）；
//   ② 脚本不写输出文件 → 输出图=输入图（与旧版透传行为一致）。
#include "ScriptNode.h"
#include "DataObject.h"
#include "ScriptSecurityPolicy.h"
#include "OpencvUtil.h"

#include <halconcpp/HalconCpp.h>
#include <opencv2/imgproc.hpp>
#include <QtTest/QtTest>
#include <vector>

class ScriptImageIoTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void scriptCanReturnAnImage();
    void noOutputFileFallsBackToPassthrough();
};

using namespace HalconCpp;

namespace {

HImage patternImage(int w = 16, int h = 12)
{
    std::vector<uchar> px(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            px[size_t(y * w + x)] = uchar((x * 7 + y * 11) % 251);
    HImage img;
    GenImage1(&img, "byte", w, h, reinterpret_cast<Hlong>(px.data()));
    return img;
}

} // namespace

void ScriptImageIoTest::initTestCase()
{
    // 自测里不弹确认框（与 --selftest-nodes 同口径）
    ScriptSecurityPolicy::instance().setRequireConfirmation(false);
}

void ScriptImageIoTest::scriptCanReturnAnImage()
{
    const QString interp = ScriptSecurityPolicy::instance().interpreterPath(QStringLiteral("Python"));
    QVERIFY2(!interp.isEmpty(), "未解析到 Python 解释器：脚本节点本身也无法执行");

    ScriptNode node;
    node.init();
    node.setParam(QStringLiteral("language"), QStringLiteral("Python"));
    node.setParam(QStringLiteral("scriptContent"), QStringLiteral(
        "import shutil, sys\n"
        "inp = out = None\n"
        "for a in sys.argv[1:]:\n"
        "    if not a.startswith('--vfp-image-'): continue\n"
        "    k, _, v = a.partition('=')\n"
        "    if k == '--vfp-image-in': inp = v\n"
        "    if k == '--vfp-image-out': out = v\n"
        "assert inp and out, 'missing vfp image args'\n"
        "shutil.copyfile(inp, out)\n"
        "print('echo-back')\n"));
    node.setInputImage(patternImage());
    node.run(false);

    QVERIFY2(node.getParam(QStringLiteral("moduleStatus")).toBool(),
             qPrintable(node.getParam(QStringLiteral("lastOutput")).toString()));
    QVERIFY(node.getParam(QStringLiteral("lastOutput")).toString().contains(QStringLiteral("echo-back")));

    HImage out(node.getOutputImage());
    QVERIFY2(out.IsInitialized(), "脚本回传的图没有成为输出图");
    cv::Mat a = OpencvUtil::himageToMat(patternImage());
    cv::Mat b = OpencvUtil::himageToMat(out);
    QCOMPARE(b.rows, a.rows);
    QCOMPARE(b.cols, a.cols);
    QVERIFY2(cv::countNonZero(a != b) == 0, "回传图与输入图不一致");
}

void ScriptImageIoTest::noOutputFileFallsBackToPassthrough()
{
    ScriptNode node;
    node.init();
    node.setParam(QStringLiteral("language"), QStringLiteral("Python"));
    node.setParam(QStringLiteral("scriptContent"),
                  QStringLiteral("import sys\nprint('no image', len(sys.argv))\n"));
    node.setInputImage(patternImage());
    node.run(false);

    QVERIFY(node.getParam(QStringLiteral("moduleStatus")).toBool());
    cv::Mat a = OpencvUtil::himageToMat(patternImage());
    cv::Mat b = OpencvUtil::himageToMat(HImage(node.getOutputImage()));
    QCOMPARE(b.rows, a.rows);
    QCOMPARE(b.cols, a.cols);
    QVERIFY2(cv::countNonZero(a != b) == 0, "未产出图时应透传输入图");
}

QTEST_MAIN(ScriptImageIoTest)
#include "script_image_io_test.moc"
