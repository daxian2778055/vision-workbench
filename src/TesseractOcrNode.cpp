#include "TesseractOcrNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <tesseract/baseapi.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
    });
    m_params[QStringLiteral("ocrText")] = QString();
    m_params[QStringLiteral("confidence")] = 0.0;
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

        // 使用 RAII 方式管理 text 内存，防止异常导致内存泄漏
        char *text = m_api->GetUTF8Text();
        const float conf = m_api->MeanTextConf();

        // 使用 QScopedArrayPointer 自动管理内存
        QScopedArrayPointer<char> textGuard(text);
        const QString result = text ? QString::fromUtf8(text) : QString();
        // text 会在 textGuard 析构时自动释放

        const QString trimmed = result.trimmed();
        m_params[QStringLiteral("ocrText")] = trimmed;
        m_params[QStringLiteral("confidence")] = static_cast<double>(conf);
        m_params["moduleStatus"] = !trimmed.isEmpty();
        m_outputImage = m_inputImage;

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(trimmed);
        setOutputData(1, strObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "TesseractOcrNode error:" << e.what();
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
