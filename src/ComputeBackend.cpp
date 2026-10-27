#include "ComputeBackend.h"

#include <QSettings>

#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>

ComputeBackend &ComputeBackend::instance()
{
    static ComputeBackend s;
    return s;
}

void ComputeBackend::init()
{
    const QString m = QSettings().value(QStringLiteral("compute/backend"),
                                        QStringLiteral("auto")).toString();
    if (m == QLatin1String("on"))
        m_mode = Mode::OpenCL;
    else if (m == QLatin1String("off"))
        m_mode = Mode::Cpu;
    else
        m_mode = Mode::Auto;
    apply();
}

void ComputeBackend::setMode(Mode m)
{
    m_mode = m;
    QSettings s;
    s.setValue(QStringLiteral("compute/backend"),
               m == Mode::OpenCL ? QStringLiteral("on")
               : m == Mode::Cpu  ? QStringLiteral("off")
                                 : QStringLiteral("auto"));
    apply();
}

void ComputeBackend::apply()
{
    // T-API 是 Mat 层面的透明加速：这里只控制进程级开关，各算子的 cv:: 调用不动
    switch (m_mode) {
    case Mode::OpenCL:
        cv::ocl::setUseOpenCL(true);
        break;
    case Mode::Cpu:
        cv::ocl::setUseOpenCL(false);
        break;
    case Mode::Auto:
        cv::ocl::setUseOpenCL(cv::ocl::haveOpenCL());
        break;
    }
}

bool ComputeBackend::openclAvailable() const
{
    return cv::ocl::haveOpenCL();
}

bool ComputeBackend::openclActive() const
{
    return cv::ocl::haveOpenCL() && cv::ocl::useOpenCL();
}

QString ComputeBackend::describe() const
{
    if (openclActive()) {
        QString name;
        try {
            const cv::ocl::Device d = cv::ocl::Device::getDefault();
            name = QString::fromStdString(d.name());
            if (name.trimmed().isEmpty())
                name = QString::fromStdString(d.vendorName());
        } catch (const cv::Exception &) {
            name = QStringLiteral("?");
        }
        return QStringLiteral("OpenCL: %1（加速）").arg(name.trimmed());
    }
    if (!openclAvailable())
        return QStringLiteral("CPU（回退：本机无 OpenCL 设备）");
    return QStringLiteral("CPU（回退：已在计算后端中关闭）");
}
