// 计算后端（2026-10-27）：OpenCV T-API 透明加速的开关与正确性守卫。
//   ① 三种模式的开/关语义与配置持久化；
//   ② 加速开启前后**结果必须逐像素一致**（T-API 的正确性底线）；
//   ③ describe() 必须说清"加速/回退"，性能面板直接显示它。
#include "ComputeBackend.h"

#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgproc.hpp>
#include <QSettings>
#include <QtTest/QtTest>

class ComputeBackendTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void modeSwitchTogglesOpenCl();
    void resultsAreIdenticalWithAndWithoutOpenCl();
    void describeAlwaysTellsTheTruth();
};

void ComputeBackendTest::initTestCase()
{
    // 与产品同一配置作用域（QSettings 默认构造按 org/app 命名空间隔离；
    // 测试进程没设 org 时读写会落到另一处，读回为空——2026-10-27 实测）
    QCoreApplication::setOrganizationName(QStringLiteral("VisionFlowPlatform"));
    QCoreApplication::setApplicationName(QStringLiteral("VisionFlow"));
    // 不受上次运行残留配置影响
    QSettings().remove(QStringLiteral("compute/backend"));
}

void ComputeBackendTest::modeSwitchTogglesOpenCl()
{
    auto &cb = ComputeBackend::instance();

    cb.setMode(ComputeBackend::Mode::Cpu);
    QVERIFY2(!cv::ocl::useOpenCL(), "仅 CPU 模式下 OpenCL 仍开着");
    QCOMPARE(QSettings().value(QStringLiteral("compute/backend")).toString(),
             QStringLiteral("off"));

    cb.setMode(ComputeBackend::Mode::Auto);
    QCOMPARE(cv::ocl::useOpenCL(), cv::ocl::haveOpenCL());   // auto = 有就用

    cb.setMode(ComputeBackend::Mode::OpenCL);
    QCOMPARE(cv::ocl::useOpenCL(), cv::ocl::haveOpenCL());   // 无设备时也只会是 false
    QCOMPARE(QSettings().value(QStringLiteral("compute/backend")).toString(),
             QStringLiteral("on"));
}

void ComputeBackendTest::resultsAreIdenticalWithAndWithoutOpenCl()
{
    // 16 位图也要一致（本轮高价值面：16 位经桥接进 OpenCV）
    cv::Mat src(128, 96, CV_16UC1);
    for (int y = 0; y < src.rows; ++y)
        for (int x = 0; x < src.cols; ++x)
            src.at<quint16>(y, x) = quint16((x * 37 + y * 91) % 65535);

    cv::Mat a, b;
    ComputeBackend::instance().setMode(ComputeBackend::Mode::Cpu);
    cv::GaussianBlur(src, a, cv::Size(5, 5), 1.2);
    cv::morphologyEx(a, a, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));

    ComputeBackend::instance().setMode(ComputeBackend::Mode::OpenCL);
    cv::GaussianBlur(src, b, cv::Size(5, 5), 1.2);
    cv::morphologyEx(b, b, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));

    QCOMPARE(b.rows, a.rows);
    QCOMPARE(b.cols, a.cols);
    // 允许极小的浮点/舍入差（T-API 实现可能不同），16 位图上给 1 LSB 容差
    double maxDiff = 0;
    for (int y = 0; y < a.rows; ++y)
        for (int x = 0; x < a.cols; ++x)
            maxDiff = std::max(maxDiff, std::abs(double(a.at<quint16>(y, x)) - double(b.at<quint16>(y, x))));
    QVERIFY2(maxDiff <= 1.0, qPrintable(QStringLiteral("加速前后结果不一致，最大差 %1").arg(maxDiff)));
}

void ComputeBackendTest::describeAlwaysTellsTheTruth()
{
    auto &cb = ComputeBackend::instance();
    cb.setMode(ComputeBackend::Mode::Cpu);
    QString s = cb.describe();
    QVERIFY2(!s.isEmpty(), "describe 为空");
    QVERIFY2(s.contains(QStringLiteral("CPU")), qPrintable(s));

    cb.setMode(ComputeBackend::Mode::OpenCL);
    s = cb.describe();
    QVERIFY2(!s.isEmpty(), "describe 为空");
    if (cb.openclActive())
        QVERIFY2(s.contains(QStringLiteral("加速")), qPrintable(s));
    else
        QVERIFY2(s.contains(QStringLiteral("回退")), qPrintable(s));
}

QTEST_MAIN(ComputeBackendTest)
#include "compute_backend_test.moc"
