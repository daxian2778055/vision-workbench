#include "OpencvCaliperNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <cmath>

OpencvCaliperNode::OpencvCaliperNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("卡尺测量"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvCaliperNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("边缘点"), PortDataType::Measure);
    addOutputPort(QStringLiteral("边缘数"), PortDataType::Number);
    registerParams({
        makeDoubleParam(QStringLiteral("row1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("起点行")),
        makeDoubleParam(QStringLiteral("col1"), 100.0, 0.0, 100000.0,
                        QStringLiteral("起点列")),
        makeDoubleParam(QStringLiteral("row2"), 200.0, 0.0, 100000.0,
                        QStringLiteral("终点行")),
        makeDoubleParam(QStringLiteral("col2"), 100.0, 0.0, 100000.0,
                        QStringLiteral("终点列")),
        makeIntParam(QStringLiteral("halfWidth"), 10, 1, 200,
                     QStringLiteral("法线方向测量半宽"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("step"), 5.0, 1.0, 100.0,
                        QStringLiteral("沿直线采样步长"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("threshold"), 10.0, 0.0, 255.0,
                        QStringLiteral("边缘梯度阈值")),
        makeIntParam(QStringLiteral("polarity"), 0, 0, 2,
                     QStringLiteral("极性：0=任意，1=亮到暗，2=暗到亮")),
    });
    m_params[QStringLiteral("edgeCount")] = 0;
}

void OpencvCaliperNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray = OpencvUtil::himageToMat(input);
        if (gray.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (gray.channels() == 3) {
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
        }

        const double r1 = m_params.value(QStringLiteral("row1"), 100.0).toDouble();
        const double c1 = m_params.value(QStringLiteral("col1"), 100.0).toDouble();
        const double r2 = m_params.value(QStringLiteral("row2"), 200.0).toDouble();
        const double c2 = m_params.value(QStringLiteral("col2"), 100.0).toDouble();
        const int half = m_params.value(QStringLiteral("halfWidth"), 10).toInt();
        const double step = m_params.value(QStringLiteral("step"), 5.0).toDouble();
        const double thr = m_params.value(QStringLiteral("threshold"), 10.0).toDouble();
        const int polarity = m_params.value(QStringLiteral("polarity"), 0).toInt();

        const double dx = c2 - c1;
        const double dy = r2 - r1;
        const double L = std::hypot(dx, dy);
        if (L < 1.0) {
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            return;
        }
        const double ux = dx / L;   // 直线方向单位向量
        const double uy = dy / L;
        const double nx = -uy;      // 法线方向单位向量
        const double ny = ux;

        const int K = std::max(1, static_cast<int>(L / step));
        const int M = half * 2 + 1; // 剖面长度

        // 结果：边缘点（图像坐标 col,row）
        std::vector<double> edgeCols, edgeRows;

        for (int k = 0; k <= K; ++k) {
            const double t = (K == 0) ? 0.0 : static_cast<double>(k) / K;
            const double cx = c1 + dx * t;
            const double cy = r1 + dy * t;

            // 法线方向一维剖面（双线性插值）
            std::vector<float> profile(M);
            for (int m = 0; m < M; ++m) {
                const double off = (m - half);
                const double px = cx + nx * off;
                const double py = cy + ny * off;
                if (px < 0 || py < 0 || px >= gray.cols - 1 || py >= gray.rows - 1) {
                    profile[m] = 0.0f;
                    continue;
                }
                const int x0 = static_cast<int>(px);
                const int y0 = static_cast<int>(py);
                const double fx = px - x0;
                const double fy = py - y0;
                const float v = static_cast<float>(
                    gray.at<uchar>(y0, x0) * (1 - fx) * (1 - fy)
                    + gray.at<uchar>(y0, x0 + 1) * fx * (1 - fy)
                    + gray.at<uchar>(y0 + 1, x0) * (1 - fx) * fy
                    + gray.at<uchar>(y0 + 1, x0 + 1) * fx * fy);
                profile[m] = v;
            }

            // 一阶梯度
            std::vector<float> grad(M - 1);
            for (int m = 0; m < M - 1; ++m) {
                grad[m] = profile[m + 1] - profile[m];
            }

            // 收集所有满足阈值的跨零极值点（一个窗口可输出多个边缘，对应 measure_select='all'）
            for (int m = 1; m < M - 2; ++m) {
                const float gPrev = grad[m - 1];
                const float gNext = grad[m + 1];
                // 局部极值：梯度符号跨零
                if (!((gPrev >= 0.0f && gNext <= 0.0f) || (gPrev <= 0.0f && gNext >= 0.0f))) {
                    continue;
                }
                if (std::fabs(grad[m]) < thr) continue;

                // 极性过滤：亮到暗 = 梯度为负；暗到亮 = 梯度为正
                if (polarity == 1 && grad[m] > 0) continue;   // 亮→暗
                if (polarity == 2 && grad[m] < 0) continue;   // 暗→亮

                // 亚像素：抛物线插值（用 m-1, m, m+1 的梯度幅值）
                double sub = static_cast<double>(m);
                const double g0 = std::fabs(grad[m - 1]);
                const double g1 = std::fabs(grad[m]);
                const double g2 = std::fabs(grad[m + 1]);
                const double denom = g0 - 2 * g1 + g2;
                if (std::fabs(denom) > 1e-9) {
                    sub += 0.5 * (g0 - g2) / denom;
                }

                const double off = sub - half;
                edgeCols.push_back(cx + nx * off);
                edgeRows.push_back(cy + ny * off);
            }
        }

        m_params[QStringLiteral("edgeCount")] = static_cast<int>(edgeCols.size());
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;

        // 输出边缘点（Measure：value=点数，extraValues=[col0,row0,col1,row1,...]）
        QVector<double> flat;
        for (size_t i = 0; i < edgeCols.size(); ++i) {
            flat.append(edgeCols[i]);
            flat.append(edgeRows[i]);
        }
        MeasureResult res;
        res.type = QStringLiteral("caliper");
        res.valueName = QStringLiteral("边缘点");
        res.valid = !edgeCols.empty();
        res.value = static_cast<double>(edgeCols.size());
        res.extraValues = flat;
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);

        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(edgeCols.size()));
        setOutputData(2, numObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvCaliperNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvCaliperNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvCaliperNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

RoiShape OpencvCaliperNode::geometryRoi() const
{
    RoiShape s;
    s.type = RoiType::Line;
    s.p1 = QPointF(m_params.value(QStringLiteral("col1"), 100.0).toDouble(),
                   m_params.value(QStringLiteral("row1"), 100.0).toDouble());
    s.p2 = QPointF(m_params.value(QStringLiteral("col2"), 100.0).toDouble(),
                   m_params.value(QStringLiteral("row2"), 200.0).toDouble());
    return s;
}

void OpencvCaliperNode::applyGeometryRoi(const RoiShape &shape)
{
    if (shape.type != RoiType::Line)
        return;
    setParam(QStringLiteral("col1"), shape.p1.x());
    setParam(QStringLiteral("row1"), shape.p1.y());
    setParam(QStringLiteral("col2"), shape.p2.x());
    setParam(QStringLiteral("row2"), shape.p2.y());
}
