#include "TesseractOcrNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <tesseract/baseapi.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QWidget>

TesseractOcrNode::TesseractOcrNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("TesseractOCR"));
    m_type = SHAPE_ANALYSIS;
}

TesseractOcrNode::~TesseractOcrNode()
{
    if (m_api) {
        m_api->End();
        delete m_api;
        m_api = nullptr;
    }
}

void TesseractOcrNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("识别文本"), PortDataType::String);
    registerParams({
        makeStringParam(QStringLiteral("language"), QStringLiteral("eng"),
                        QStringLiteral("语言（eng/chi_sim，逗号分隔多语言）")),
        makeStringParam(QStringLiteral("tessdataPath"), QString(),
                        QStringLiteral("语言数据目录（留空自动查找 exe 旁 tessdata）")),
        makeEnumParam(QStringLiteral("pageSegMode"), 0,
                      {QStringLiteral("自动"), QStringLiteral("单行"),
                       QStringLiteral("单块"), QStringLiteral("稀疏文本"),
                       QStringLiteral("单字")},
                      QStringLiteral("版面模式")),
        makeStringParam(QStringLiteral("charWhitelist"), QString(),
                        QStringLiteral("字符白名单（如 0123456789.，留空=不限）")),
        makeDoubleParam(QStringLiteral("minConfidence"), 0.0, 0.0, 100.0,
                        QStringLiteral("最低平均置信度（0=不校验）")),
    });
    m_params[QStringLiteral("ocrText")] = QString();
    m_params[QStringLiteral("confidence")] = 0.0;
    m_params[QStringLiteral("ocrTextLength")] = 0;
    m_params[QStringLiteral("ocrLineCount")] = 0;
    m_params[QStringLiteral("ocrWordCount")] = 0;
}

void TesseractOcrNode::run(bool /*autoSwitch*/)
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
        // 非 8 位输入的显式拒绝（2026-10-25 复核 P2）：桥接修好后 16 位图会如实返回 CV_16U，
        // 而这里的阈值 0-255 / at<uchar> / 外部 8 位入口都按 8 位写死——静默错值比判红更糟。
        {
            QString depthWhy;
            if (!OpencvUtil::requireGray8(gray, &depthWhy))
                throw std::runtime_error(depthWhy.toStdString());
        }
        if (gray.channels() == 3) {
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
        }

        const QString lang = m_params.value(QStringLiteral("language"), QStringLiteral("eng")).toString();
        QString tessdata = m_params.value(QStringLiteral("tessdataPath"), QString()).toString().trimmed();
        if (tessdata.isEmpty()) {
            // 自动查找：exe 旁 tessdata/ > 仓库 thirdparty/tesseract/tessdata
            const QString exeDir = QCoreApplication::applicationDirPath();
            const QString exeTess = exeDir + QStringLiteral("/tessdata");
            if (QDir(exeTess).exists()) {
                tessdata = exeTess;
            } else {
                tessdata = QStringLiteral("thirdparty/tesseract/tessdata");
            }
        }

        // 语言或数据路径变化时重新初始化引擎
        if (!m_api || m_loadedLang != lang || m_loadedTessdata != tessdata) {
            if (m_api) {
                m_api->End();
                delete m_api;
                m_api = nullptr;
            }
            m_api = new tesseract::TessBaseAPI();
            if (m_api->Init(tessdata.toLocal8Bit().constData(), lang.toLocal8Bit().constData())) {
                delete m_api;
                m_api = nullptr;
                m_params[QStringLiteral("ocrText")] =
                    QStringLiteral("Tesseract 初始化失败（检查语言数据目录）");
                m_params["moduleStatus"] = false;
                m_outputImage = m_inputImage;
                setOutputData(1, QSharedPointer<DataObject>());
                return;
            }
            m_loadedLang = lang;
            m_loadedTessdata = tessdata;
        }

        m_api->SetImage(gray.data, gray.cols, gray.rows, 1, static_cast<int>(gray.step));
        m_api->SetSourceResolution(300);

        // 版面模式 / 字符白名单：每次执行前下发（引擎不重建，参数变化即时生效）
        tesseract::PageSegMode psm = tesseract::PSM_AUTO;
        switch (m_params.value(QStringLiteral("pageSegMode"), 0).toInt()) {
        case 1: psm = tesseract::PSM_SINGLE_LINE; break;
        case 2: psm = tesseract::PSM_SINGLE_BLOCK; break;
        case 3: psm = tesseract::PSM_SPARSE_TEXT; break;
        case 4: psm = tesseract::PSM_SINGLE_CHAR; break;
        default: psm = tesseract::PSM_AUTO; break;
        }
        m_api->SetPageSegMode(psm);
        const QString whitelist =
            m_params.value(QStringLiteral("charWhitelist"), QString()).toString().trimmed();
        // 留空时清掉上一次的白名单（SetVariable 是粘性的，不清会沿用旧限制）
        m_api->SetVariable("tessedit_char_whitelist", whitelist.toUtf8().constData());

        // 使用 RAII 方式管理 text 内存，防止异常导致内存泄漏
        char *text = m_api->GetUTF8Text();
        const float conf = m_api->MeanTextConf();

        // 使用 QScopedArrayPointer 自动管理内存
        QScopedArrayPointer<char> textGuard(text);
        const QString result = text ? QString::fromUtf8(text) : QString();
        // text 会在 textGuard 析构时自动释放

        const QString trimmed = result.trimmed();
        const double meanConf = static_cast<double>(conf);
        const double minConf =
            m_params.value(QStringLiteral("minConfidence"), 0.0).toDouble();

        m_params[QStringLiteral("ocrText")] = trimmed;
        m_params[QStringLiteral("confidence")] = meanConf;
        m_params[QStringLiteral("ocrTextLength")] = trimmed.size();
        m_params[QStringLiteral("ocrLineCount")] =
            trimmed.isEmpty()
                ? 0
                : trimmed.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                Qt::SkipEmptyParts).size();
        m_params[QStringLiteral("ocrWordCount")] =
            trimmed.isEmpty()
                ? 0
                : trimmed.split(QRegularExpression(QStringLiteral("\\s+")),
                                Qt::SkipEmptyParts).size();

        // 置信度门槛：低于阈值判红（文本照样输出，原因写进 lastError）
        if (!trimmed.isEmpty() && minConf > 0.0 && meanConf < minConf) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("识别平均置信度 %1 低于阈值 %2")
                    .arg(meanConf, 0, 'f', 1)
                    .arg(minConf, 0, 'f', 1);
            m_params["moduleStatus"] = false;
        } else {
            m_params["moduleStatus"] = !trimmed.isEmpty();
        }
        m_outputImage = m_inputImage;

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(trimmed);
        setOutputData(1, strObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "TesseractOcrNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *TesseractOcrNode::createParamPanel()
{
    return createAutoParamPanel();
}

void TesseractOcrNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

QVector<NodeResultField> TesseractOcrNode::resultDisplayFields() const
{
    // VM「模块结果」口径：模块级 耗时/模块状态 + 识别文本/置信度/长度/行数/词数。
    QVector<NodeResultField> fields = HalconNode::resultDisplayFields();
    fields << resultValue(QStringLiteral("ocrText"), QStringLiteral("识别文本"))
           << resultValue(QStringLiteral("confidence"), QStringLiteral("置信度"),
                          QString(), 4)
           << resultValue(QStringLiteral("ocrTextLength"), QStringLiteral("文本长度"),
                          QString(), 0)
           << resultValue(QStringLiteral("ocrLineCount"), QStringLiteral("行数"),
                          QString(), 0)
           << resultValue(QStringLiteral("ocrWordCount"), QStringLiteral("词数"),
                          QString(), 0);
    return fields;
}
