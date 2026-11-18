#include "OutputImageNode.h"
#include "OpencvUtil.h"
#include "ProjectPaths.h"
#include "LargeFileImageStore.h"
#include "DataObject.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QSignalBlocker>

#include <opencv2/imgcodecs.hpp>

using namespace HalconCpp;

OutputImageNode::OutputImageNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("输出图像"));
    m_type = IMAGE_ACQUISITION;
}

void OutputImageNode::init()
{
    HalconNode::init();
    registerParams({
        makeFilePathParam(QStringLiteral("saveDir"), QStringLiteral("captures"),
                          QStringLiteral("保存目录（空=方案目录）")),
        makeStringParam(QStringLiteral("namePattern"), QStringLiteral("IMG_%yyyyMMdd%_%HHmmss%_%n"),
                        QStringLiteral("命名规则（%yyyyMMdd%/%HHmmss%/%n）")),
        makeEnumParam(QStringLiteral("format"), 0,
                      { QStringLiteral("png"), QStringLiteral("bmp"), QStringLiteral("jpg"),
                        QStringLiteral("tif") },
                      QStringLiteral("图片格式")),
        makeBoolParam(QStringLiteral("overwrite"), false, QStringLiteral("同名覆盖（关=自动加序号）")),
    });
}

QString OutputImageNode::resolveSavePath(QString *error) const
{
    const QString dirRaw = m_params.value(QStringLiteral("saveDir")).toString().trimmed();
    const QString dir = ProjectPaths::resolveExecutionPath(dirRaw);
    if (dir.isEmpty()) {
        if (error) *error = QStringLiteral("保存目录为空");
        return QString();
    }
    QString name = m_params.value(QStringLiteral("namePattern")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) *error = QStringLiteral("命名规则为空");
        return QString();
    }
    const QDateTime now = QDateTime::currentDateTime();
    name.replace(QStringLiteral("%yyyyMMdd%"), now.toString(QStringLiteral("yyyyMMdd")));
    name.replace(QStringLiteral("%HHmmss%"), now.toString(QStringLiteral("HHmmss")));
    // 清掉路径分隔符与非法字符，避免命名规则越出保存目录
    name.remove(QLatin1Char('/'));
    name.remove(QLatin1Char('\\'));
    const int fmtIdx = m_params.value(QStringLiteral("format"), 0).toInt();
    static const char *kExt[] = { ".png", ".bmp", ".jpg", ".tif" };
    const QString ext = QString::fromLatin1(kExt[fmtIdx >= 0 && fmtIdx < 4 ? fmtIdx : 0]);
    if (!name.endsWith(ext, Qt::CaseInsensitive))
        name += ext;
    const bool overwrite = m_params.value(QStringLiteral("overwrite"), false).toBool();
    QString candidate = QDir(dir).filePath(name);
    if (!overwrite) {
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        const QString stem = dot > 0 ? name.left(dot) : name;
        for (int seq = 1; seq <= 99999; ++seq) {
            candidate = QDir(dir).filePath(
                stem + QStringLiteral("_%1").arg(seq, 3, 10, QLatin1Char('0')) + ext);
            if (!QFileInfo::exists(candidate))
                break;
        }
    }
    return candidate;
}

void OutputImageNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("moduleStatus")] = false;
    m_params[QStringLiteral("lastError")] = QString();
    m_params[QStringLiteral("lastSavedPath")] = QString();
    try {
        if (!m_inputImage.IsInitialized()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("没有输入图像");
            m_outputImage.Clear();
            return;
        }
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("输入图像为空");
            m_outputImage.Clear();
            return;
        }

        // 大文件存图策略：开启后忽略本节点路径，攒批写 .dat（与 WriteFileNode 同口径）
        if (LargeFileImageStore *store = LargeFileImageStore::instance(); store->enabled()) {
            QString batch, why;
            if (!store->append(src, fullName(), &batch, &why)) {
                m_params[QStringLiteral("lastError")] =
                    LargeFileImageStore::failureMessage(why);
                m_outputImage.Clear();
                return;
            }
            m_params[QStringLiteral("lastSavedPath")] =
                batch.isEmpty() ? QStringLiteral("（待写入批次）") : batch;
            m_params[QStringLiteral("moduleStatus")] = true;
            m_outputImage = m_inputImage;
            return;
        }

        QString why;
        const QString path = resolveSavePath(&why);
        if (path.isEmpty()) {
            m_params[QStringLiteral("lastError")] = why;
            m_outputImage.Clear();
            return;
        }
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("无法创建保存目录");
            m_outputImage.Clear();
            return;
        }
        if (!cv::imwrite(path.toStdString(), src)) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("图像写入失败（格式/权限？）：%1").arg(path);
            m_outputImage.Clear();
            return;
        }
        m_params[QStringLiteral("lastSavedPath")] = path;
        m_params[QStringLiteral("moduleStatus")] = true;
        m_outputImage = m_inputImage;
    } catch (const HException &e) {
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.ErrorMessage().TextA());
        m_outputImage.Clear();
    } catch (const std::exception &e) {
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.what());
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = QStringLiteral("未知异常");
        m_outputImage.Clear();
    }
}

QWidget *OutputImageNode::createParamPanel()
{
    QWidget *panel = createAutoParamPanel();
    auto *btn = new QPushButton(QStringLiteral("选择保存目录…"));
    btn->setObjectName(QStringLiteral("outputImageChooseDir"));
    connect(btn, &QPushButton::clicked, this, [this, panel]() {
        QLineEdit *edit = panel->findChild<QLineEdit *>(QStringLiteral("saveDir"));
        const QString cur = edit ? edit->text()
                                 : m_params.value(QStringLiteral("saveDir")).toString();
        const QString dir = QFileDialog::getExistingDirectory(
            nullptr, QStringLiteral("选择保存目录"), cur);
        if (dir.isEmpty())
            return;
        const QString rel = ProjectPaths::makeSchemeRelativeIfInside(
            ProjectPaths::currentSchemeFilePath(), dir);
        if (edit) {
            QSignalBlocker b(edit);
            edit->setText(rel);
        }
        setParam(QStringLiteral("saveDir"), rel);
    });
    if (auto *lay = qobject_cast<QVBoxLayout *>(panel->layout()))
        lay->addWidget(btn);
    return panel;
}

void OutputImageNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}
