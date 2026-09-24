#include "AnomalyBaseline.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>

namespace {

/// 8U/32F 单通道 → CV_32F；不是单通道灰度图则返回空
cv::Mat asFloatGray(const cv::Mat &m)
{
    if (m.empty() || m.channels() != 1)
        return cv::Mat();
    cv::Mat f;
    if (m.depth() == CV_32F)
        return m.clone();
    m.convertTo(f, CV_32F);
    return f;
}

/// 把连续平面写/读进流（本机字节序；文件头里已写明版本，跨平台读取由调用方负责endianness）
void writePlane(QDataStream &s, const cv::Mat &plane)
{
    const size_t bytes = plane.total() * plane.elemSize();
    s.writeRawData(reinterpret_cast<const char *>(plane.data), int(bytes));
}

bool readPlane(QDataStream &s, cv::Mat &plane, int w, int h)
{
    plane.create(h, w, CV_32FC1);
    const size_t bytes = plane.total() * plane.elemSize();
    return s.readRawData(reinterpret_cast<char *>(plane.data), int(bytes)) == int(bytes);
}

}   // namespace

void AnomalyBaseline::clear()
{
    m_mean.release();
    m_m2.release();
    m_ref.release();
    m_n = 0;
}

bool AnomalyBaseline::addSample(const cv::Mat &gray8)
{
    const cv::Mat x = asFloatGray(gray8);
    if (x.empty())
        return false;
    if (m_n == 0) {
        m_mean = x;
        m_m2 = cv::Mat::zeros(x.size(), CV_32FC1);
        m_ref = x.clone();
        m_n = 1;
        return true;
    }
    if (x.size() != m_mean.size())
        return false;

    // Welford：M2 += (x-mean_old)*(x-mean_new)，比"先存 N 张再求方差"省一整份图像缓冲
    const cv::Mat delta = x - m_mean;
    const cv::Mat newMean = m_mean + delta * (1.0 / double(m_n + 1));
    m_m2 = m_m2 + delta.mul(x - newMean);
    m_mean = newMean;
    ++m_n;
    return true;
}

cv::Mat AnomalyBaseline::sigma() const
{
    if (m_mean.empty())
        return cv::Mat();
    cv::Mat s;
    cv::sqrt(m_m2 * (1.0 / double(std::max(1, m_n))), s);
    return s;
}

cv::Mat AnomalyBaseline::scoreMap(const cv::Mat &gray8, double sigmaFloor) const
{
    if (m_mean.empty())
        return cv::Mat();
    const cv::Mat x = asFloatGray(gray8);
    if (x.empty() || x.size() != m_mean.size())
        return cv::Mat();

    cv::Mat denom = sigma();
    // floor 必须先于除法生效：n=1 时 σ 恒为 0，没有 floor 整张教学图都会被判异常
    cv::max(denom, float(std::max(0.01, sigmaFloor)), denom);
    cv::Mat diff;
    cv::absdiff(x, m_mean, diff);
    cv::Mat score;
    cv::divide(diff, denom, score);
    return score;
}

bool AnomalyBaseline::save(const QString &path, QString *error) const
{
    const auto fail = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (m_mean.empty())
        return fail(QStringLiteral("基线为空，无内容可保存"));
    const QFileInfo fi(path);
    if (!fi.dir().exists() && !QDir().mkpath(fi.absolutePath()))
        return fail(QStringLiteral("无法创建目录：%1").arg(fi.absolutePath()));

    // 原子写：教学到一半断电留下半个基线，比没有基线更糟（下次推理会静默用坏数据）
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("基线文件打开失败：%1（%2）").arg(path, file.errorString()));
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);
    s << kMagic << kVersion << qint32(m_mean.cols) << qint32(m_mean.rows) << qint32(m_n);
    writePlane(s, m_mean);
    writePlane(s, m_m2);
    writePlane(s, m_ref);
    if (!file.commit())
        return fail(QStringLiteral("基线文件提交失败：%1（%2）").arg(path, file.errorString()));
    return true;
}

bool AnomalyBaseline::load(const QString &path, QString *error)
{
    const auto fail = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("基线文件打开失败：%1（%2）").arg(path, file.errorString()));
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);
    qint32 magic = 0, version = 0, w = 0, h = 0, n = 0;
    s >> magic >> version >> w >> h >> n;
    if (s.status() != QDataStream::Ok || magic != kMagic)
        return fail(QStringLiteral("不是异常基线文件：%1").arg(path));
    if (version != kVersion)
        return fail(QStringLiteral("基线版本不兼容（文件 v%1，本程序 v%2）").arg(version).arg(kVersion));
    if (w < 1 || h < 1 || n < 1)
        return fail(QStringLiteral("基线文件头非法（%1×%2，样本数 %3）").arg(w).arg(h).arg(n));

    cv::Mat mean, m2, ref;
    if (!readPlane(s, mean, w, h) || !readPlane(s, m2, w, h) || !readPlane(s, ref, w, h))
        return fail(QStringLiteral("基线文件内容不完整（读取长度不符）：%1").arg(path));

    m_mean = mean;
    m_m2 = m2;
    m_ref = ref;
    m_n = int(n);
    return true;
}

// ───────────────────────────── 后端 B：PCA 低秩残差 ─────────────────────────────

bool AnomalyPcaModel::train(const std::vector<cv::Mat> &samples, int components, QString *error)
{
    const auto fail = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (samples.size() < 2)
        return fail(QStringLiteral("PCA 建模至少需要 2 个 OK 样本（一个点没有流形可言）"));
    const cv::Size first = samples.front().size();
    if (first.width < 1 || first.height < 1)
        return fail(QStringLiteral("样本尺寸非法"));
    for (const cv::Mat &s : samples) {
        if (s.size() != first || s.channels() != 1)
            return fail(QStringLiteral("样本尺寸/通道不一致，PCA 需要在同一尺寸上建模"));
    }

    const int n = int(samples.size());
    const int d = first.area();
    cv::Mat X(n, d, CV_32FC1);
    for (int i = 0; i < n; ++i) {
        cv::Mat f;
        samples[i].convertTo(f, CV_32F);
        f.reshape(1, 1).copyTo(X.row(i));
    }

    cv::Mat meanRow;
    cv::reduce(X, meanRow, 0, cv::REDUCE_AVG);   // 1×D
    cv::Mat meanTiled;
    cv::repeat(meanRow, n, 1, meanTiled);
    const cv::Mat Y = X - meanTiled;

    // 快照 PCA：D 可达数万，协方差是 D×D；改在 n×n 的 Gram 矩阵上特征分解，
    // 主成分再由 Y 映回 D 维（q ≤ n-1，与直接解协方差等价）
    cv::Mat gram;
    cv::gemm(Y, Y, 1.0 / double(n - 1), cv::noArray(), 0.0, gram, cv::GEMM_2_T);
    cv::Mat evals, evecs;
    cv::eigen(gram, evals, evecs);   // 降序

    const int qMax = std::min(components, n - 1);
    std::vector<cv::Mat> kept;
    for (int i = 0; i < qMax && i < evecs.rows; ++i) {
        cv::Mat v;
        cv::gemm(evecs.row(i), Y, 1.0, cv::noArray(), 0.0, v);   // 1×D
        const double norm = cv::norm(v);
        if (norm < 1e-8)
            continue;   // 数值上退化的方向（重复样本等）：留着会让重构除零
        v /= float(norm);
        kept.push_back(v);
    }
    if (kept.empty())
        return fail(QStringLiteral("OK 样本之间没有变化，PCA 基退化（改用逐像素统计基线）"));

    cv::Mat basis(int(kept.size()), d, CV_32FC1);
    for (size_t i = 0; i < kept.size(); ++i)
        kept[i].copyTo(basis.row(int(i)));

    // 训练残差的逐像素标准差：判据仍是"几个 σ"，与后端 A 同一把尺子
    cv::Mat basisT, proj, recon;
    cv::transpose(basis, basisT);
    cv::gemm(Y, basisT, 1.0, cv::noArray(), 0.0, proj);          // n×q
    cv::gemm(proj, basis, 1.0, cv::noArray(), 0.0, recon);       // n×D
    const cv::Mat R = Y - recon;
    cv::Mat rsigma;
    cv::reduce(R.mul(R), rsigma, 0, cv::REDUCE_SUM);
    rsigma *= float(1.0 / double(n - 1));
    cv::sqrt(rsigma, rsigma);

    m_size = first;
    m_n = n;
    m_mean = meanRow;
    m_basis = basis;
    m_rsigma = rsigma;
    return true;
}

cv::Mat AnomalyPcaModel::scoreMap(const cv::Mat &gray8, double floorValue) const
{
    if (m_basis.empty())
        return cv::Mat();
    cv::Mat f;
    gray8.convertTo(f, CV_32F);
    if (f.empty() || f.channels() != 1 || f.size() != m_size)
        return cv::Mat();

    const cv::Mat x = f.reshape(1, 1);
    const cv::Mat y = x - m_mean;
    cv::Mat basisT, proj, recon;
    cv::transpose(m_basis, basisT);
    cv::gemm(y, basisT, 1.0, cv::noArray(), 0.0, proj);
    cv::gemm(proj, m_basis, 1.0, cv::noArray(), 0.0, recon);
    cv::Mat resid;
    cv::absdiff(y, recon, resid);
    resid = resid.reshape(1, m_size.height);

    cv::Mat denom;
    cv::max(m_rsigma.reshape(1, m_size.height), float(std::max(0.01, floorValue)), denom);
    cv::Mat score;
    cv::divide(resid, denom, score);
    return score;
}

bool AnomalyPcaModel::save(const QString &path, QString *error) const
{
    const auto fail = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (m_basis.empty())
        return fail(QStringLiteral("PCA 模型为空，无内容可保存"));
    const QFileInfo fi(path);
    if (!fi.dir().exists() && !QDir().mkpath(fi.absolutePath()))
        return fail(QStringLiteral("无法创建目录：%1").arg(fi.absolutePath()));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("模型文件打开失败：%1（%2）").arg(path, file.errorString()));
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);
    const qint32 q = m_basis.rows;
    s << kMagic << kVersion << qint32(m_size.width) << qint32(m_size.height) << qint32(m_n) << q;
    writePlane(s, m_mean);
    writePlane(s, m_basis);
    writePlane(s, m_rsigma);
    if (!file.commit())
        return fail(QStringLiteral("模型文件提交失败：%1（%2）").arg(path, file.errorString()));
    return true;
}

bool AnomalyPcaModel::load(const QString &path, QString *error)
{
    const auto fail = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("模型文件打开失败：%1（%2）").arg(path, file.errorString()));
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);
    qint32 magic = 0, version = 0, w = 0, h = 0, n = 0, q = 0;
    s >> magic >> version >> w >> h >> n >> q;
    if (s.status() != QDataStream::Ok || magic != kMagic)
        return fail(QStringLiteral("不是 PCA 异常模型文件：%1（逐像素统计基线文件请选“统计基线”后端）").arg(path));
    if (version != kVersion)
        return fail(QStringLiteral("模型版本不兼容（文件 v%1，本程序 v%2）").arg(version).arg(kVersion));
    if (w < 1 || h < 1 || n < 2 || q < 1 || q > n - 1)
        return fail(QStringLiteral("模型文件头非法（%1×%2，样本 %3，主成分 %4）").arg(w).arg(h).arg(n).arg(q));

    cv::Mat mean, basis, rsigma;
    // 形状必须与 train() 的内存布局一致：mean/rsigma 是 1×D 行向量，basis 是 q×D。
    // 读成 h×w 的话总元素数相同、不会报错，但打分时 "1×D − 1×D" 就变成尺寸不符。
    if (!readPlane(s, mean, w * h, 1) || !readPlane(s, basis, w * h, q)
        || !readPlane(s, rsigma, w * h, 1))
        return fail(QStringLiteral("模型文件内容不完整：%1").arg(path));

    m_size = cv::Size(w, h);
    m_n = int(n);
    m_mean = mean;
    m_basis = basis;
    m_rsigma = rsigma;
    return true;
}
