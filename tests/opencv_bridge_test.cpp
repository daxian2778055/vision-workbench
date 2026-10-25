// OpenCV 桥接的深度保真（2026-10-25 复核 P1-1 / P2-1）。
//
// P1-1：himageToMat 取了 HALCON 的 type 却直接按 CV_8UC1 包像素——uint2（MVS 的
// Mono10/12/16、16 位图像文件）被拆成字节对，45 个调用方全部在错图上跑且判绿。
// P2-1：matToHimage 把一切非 8UC1/3 都 convertTo(CV_8U, 1.0)——ConvertImageNode 的六个
// 目标类型全部输出 byte，0/1000/60000 → 0/255/255（饱和截断，注释写"归一化"是错的）。
//
// 本用例用真 HALCON 图像做往返，不依赖相机/文件。
#include "OpencvUtil.h"
#include "ConvertImageNode.h"
#include "NodeBase.h"

#include <halconcpp/HalconCpp.h>
#include <opencv2/core.hpp>
#include <QtTest/QtTest>
#include <vector>

class OpencvBridgeTest : public QObject
{
    Q_OBJECT
private slots:
    void byteSingleChannelRoundTrip();      // 回归：8 位单通道
    void byteThreeChannelRoundTrip();       // 回归：8 位三通道
    void uint2RoundTripPreservesValues();   // P1：16 位单通道
    void uint2ThreeChannelRoundTrip();      // P1：16 位三通道
    void convertImage16uIsNotSaturated();   // P2-1：ConvertImageNode 的 16U 目标
    void realRoundTripPreservesValues();    // real（float）往返
};

using namespace HalconCpp;

namespace {

HImage makeImage1(const char *type, int w, int h, const void *data)
{
    HImage img;
    GenImage1(&img, type, w, h, reinterpret_cast<Hlong>(const_cast<void *>(data)));
    return img;
}

} // namespace

void OpencvBridgeTest::byteSingleChannelRoundTrip()
{
    std::vector<uchar> px = { 1, 2, 3, 4, 5, 6 };
    HImage img = makeImage1("byte", 3, 2, px.data());
    cv::Mat m = OpencvUtil::himageToMat(img);
    QCOMPARE(m.type(), CV_8UC1);
    QCOMPARE(m.rows, 2);
    QCOMPARE(m.cols, 3);
    QCOMPARE(int(m.at<uchar>(0, 0)), 1);
    QCOMPARE(int(m.at<uchar>(1, 2)), 6);

    HImage back = OpencvUtil::matToHimage(m);
    HTuple t, p, w, h;
    GetImagePointer1(back, &p, &t, &w, &h);
    QCOMPARE(QByteArray(t.S()), QByteArray("byte"));
    const uchar *q = reinterpret_cast<const uchar *>(p.L());
    QCOMPARE(int(q[0]), 1);
    QCOMPARE(int(q[5]), 6);
}

void OpencvBridgeTest::byteThreeChannelRoundTrip()
{
    // HALCON RGB 平面；Mat 侧应是 BGR 交织
    std::vector<uchar> r = { 10, 11 }, g = { 20, 21 }, b = { 30, 31 };
    HImage img;
    GenImage3(&img, "byte", 2, 1,
              reinterpret_cast<Hlong>(r.data()),
              reinterpret_cast<Hlong>(g.data()),
              reinterpret_cast<Hlong>(b.data()));
    cv::Mat m = OpencvUtil::himageToMat(img);
    QCOMPARE(m.type(), CV_8UC3);
    QCOMPARE(int(m.at<cv::Vec3b>(0, 0)[0]), 30);   // B
    QCOMPARE(int(m.at<cv::Vec3b>(0, 0)[1]), 20);   // G
    QCOMPARE(int(m.at<cv::Vec3b>(0, 0)[2]), 10);   // R

    HImage back = OpencvUtil::matToHimage(m);
    HTuple t, pR, pG, pB, w, h;
    GetImagePointer3(back, &pR, &pG, &pB, &t, &w, &h);
    QCOMPARE(QByteArray(t.S()), QByteArray("byte"));
    const uchar *qr = reinterpret_cast<const uchar *>(pR.L());
    const uchar *qb = reinterpret_cast<const uchar *>(pB.L());
    QCOMPARE(int(qr[0]), 10);
    QCOMPARE(int(qb[1]), 31);
}

void OpencvBridgeTest::uint2RoundTripPreservesValues()
{
    // 2×2，取值带字节序特征（0x1112 小端字节是 12 11——旧实现会把它读成两个像素）
    std::vector<quint16> px = { 0x1112, 0x1314, 0x1516, 0x1718 };
    HImage img = makeImage1("uint2", 2, 2, px.data());
    cv::Mat m = OpencvUtil::himageToMat(img);
    QCOMPARE(m.type(), CV_16UC1);          // 旧实现这里是 CV_8UC1 且数据被拆成字节对
    QCOMPARE(m.rows, 2);
    QCOMPARE(m.cols, 2);
    QCOMPARE(int(m.at<quint16>(0, 0)), 0x1112);
    QCOMPARE(int(m.at<quint16>(0, 1)), 0x1314);
    QCOMPARE(int(m.at<quint16>(1, 0)), 0x1516);
    QCOMPARE(int(m.at<quint16>(1, 1)), 0x1718);

    HImage back = OpencvUtil::matToHimage(m);
    HTuple t, p, w, h;
    GetImagePointer1(back, &p, &t, &w, &h);
    QCOMPARE(QByteArray(t.S()), QByteArray("uint2"));
    const quint16 *q = reinterpret_cast<const quint16 *>(p.L());
    QCOMPARE(int(q[0]), 0x1112);
    QCOMPARE(int(q[3]), 0x1718);
}

void OpencvBridgeTest::uint2ThreeChannelRoundTrip()
{
    std::vector<quint16> r = { 0x0102, 0x0304 }, g = { 0x0506, 0x0708 },
                         b = { 0x090A, 0x0B0C };
    HImage img;
    GenImage3(&img, "uint2", 2, 1,
              reinterpret_cast<Hlong>(r.data()),
              reinterpret_cast<Hlong>(g.data()),
              reinterpret_cast<Hlong>(b.data()));
    cv::Mat m = OpencvUtil::himageToMat(img);
    QCOMPARE(m.type(), CV_16UC3);
    QCOMPARE(int(m.at<cv::Vec3w>(0, 0)[2]), 0x0102);   // R
    QCOMPARE(int(m.at<cv::Vec3w>(0, 1)[0]), 0x0B0C);   // B

    HImage back = OpencvUtil::matToHimage(m);
    HTuple t, pR, pG, pB, w, h;
    GetImagePointer3(back, &pR, &pG, &pB, &t, &w, &h);
    QCOMPARE(QByteArray(t.S()), QByteArray("uint2"));
    const quint16 *qr = reinterpret_cast<const quint16 *>(pR.L());
    const quint16 *qb = reinterpret_cast<const quint16 *>(pB.L());
    QCOMPARE(int(qr[0]), 0x0102);
    QCOMPARE(int(qb[1]), 0x0B0C);
}

void OpencvBridgeTest::convertImage16uIsNotSaturated()
{
    // P2-1：ConvertImageNode 的 16U 目标必须真出 16 位、值不被饱和截断
    std::vector<quint16> px = { 0, 1000, 60000, 4095 };
    HImage img = makeImage1("uint2", 2, 2, px.data());

    ConvertImageNode node;
    node.init();
    node.setInputImage(img);
    node.setParam(QStringLiteral("targetType"), 3);   // CV_16U
    node.run(false);

    HImage out = node.getOutputImage();
    QVERIFY(out.IsInitialized());
    HTuple t, p, w, h;
    GetImagePointer1(out, &p, &t, &w, &h);
    // 旧实现：type=byte 且 0/1000/60000 → 0/255/255
    QCOMPARE(QByteArray(t.S()), QByteArray("uint2"));
    const quint16 *q = reinterpret_cast<const quint16 *>(p.L());
    QCOMPARE(int(q[0]), 0);
    QCOMPARE(int(q[1]), 1000);
    QCOMPARE(int(q[2]), 60000);
    QCOMPARE(int(q[3]), 4095);
}

void OpencvBridgeTest::realRoundTripPreservesValues()
{
    std::vector<float> px = { 0.5f, -1.25f, 100.0f, 0.0f };
    HImage img = makeImage1("real", 2, 2, px.data());
    cv::Mat m = OpencvUtil::himageToMat(img);
    QCOMPARE(m.type(), CV_32FC1);
    QCOMPARE(m.at<float>(0, 0), 0.5f);
    QCOMPARE(m.at<float>(1, 1), 0.0f);

    HImage back = OpencvUtil::matToHimage(m);
    HTuple t, p, w, h;
    GetImagePointer1(back, &p, &t, &w, &h);
    QCOMPARE(QByteArray(t.S()), QByteArray("real"));
    const float *q = reinterpret_cast<const float *>(p.L());
    QCOMPARE(q[1], -1.25f);
    QCOMPARE(q[2], 100.0f);
}

QTEST_MAIN(OpencvBridgeTest)
#include "opencv_bridge_test.moc"
