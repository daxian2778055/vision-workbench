#include "CodeExportDialog.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>

CodeExportDialog::CodeExportDialog(const QList<FlowScene *> &flows, QWidget *parent)
    : QDialog(parent)
    , m_flows(flows)
{
    setupUI();
    if (!m_flows.isEmpty()) {
        m_flowCombo->setCurrentIndex(0);
        generateCode(0);
    }
}

CodeExportDialog::~CodeExportDialog()
{
}

void CodeExportDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u4EE3\u7801\u5BFC\u51FA"));
    setMinimumSize(760, 520);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(QStringLiteral("<b>\u7B97\u5B50\u4EE3\u7801\u5BFC\u51FA</b>"));
    titleLabel->setStyleSheet("font-size: 14px;");
    mainLayout->addWidget(titleLabel);

    auto *topLayout = new QHBoxLayout();
    topLayout->addWidget(new QLabel(QStringLiteral("\u6D41\u7A0B:")));
    m_flowCombo = new QComboBox();
    m_flowCombo->setMinimumWidth(180);
    for (int i = 0; i < m_flows.size(); ++i) {
        FlowScene *fs = m_flows[i];
        const QString name = (fs && !fs->flowName().isEmpty())
                                ? fs->flowName()
                                : QStringLiteral("\u6D41\u7A0B %1").arg(i + 1);
        m_flowCombo->addItem(name);
    }
    topLayout->addWidget(m_flowCombo);

    topLayout->addWidget(new QLabel(QStringLiteral("\u8BED\u8A00:")));
    m_langCombo = new QComboBox();
    m_langCombo->addItem(QStringLiteral("HDevelop"), QStringLiteral("hdev"));
    m_langCombo->addItem(QStringLiteral("C++"), QStringLiteral("cpp"));
    topLayout->addWidget(m_langCombo);

    m_generateBtn = new QPushButton(QStringLiteral("\u751F\u6210\u4EE3\u7801"));
    m_generateBtn->setMinimumHeight(28);
    topLayout->addWidget(m_generateBtn);

    m_copyBtn = new QPushButton(QStringLiteral("\u590D\u5236"));
    m_copyBtn->setMinimumHeight(28);
    topLayout->addWidget(m_copyBtn);

    m_saveBtn = new QPushButton(QStringLiteral("\u4FDD\u5B58..."));
    m_saveBtn->setMinimumHeight(28);
    topLayout->addWidget(m_saveBtn);

    topLayout->addStretch();
    mainLayout->addLayout(topLayout);

    m_codeView = new QPlainTextEdit();
    m_codeView->setReadOnly(true);
    m_codeView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_codeView->setStyleSheet(
        "QPlainTextEdit { font-family: Consolas, 'Courier New'; font-size: 12px; background: #1e1e1e; color: #dcdcdc; }");
    mainLayout->addWidget(m_codeView);

    connect(m_flowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CodeExportDialog::onFlowChanged);
    connect(m_generateBtn, &QPushButton::clicked, this, &CodeExportDialog::onGenerate);
    connect(m_copyBtn, &QPushButton::clicked, this, &CodeExportDialog::onCopy);
    connect(m_saveBtn, &QPushButton::clicked, this, &CodeExportDialog::onSave);
}

void CodeExportDialog::onFlowChanged(int index)
{
    generateCode(index);
}

void CodeExportDialog::onGenerate()
{
    generateCode(m_flowCombo->currentIndex());
}

QString CodeExportDialog::nodeToHalcon(NodeBase *node) const
{
    if (!node) return QString();
    // 生成 HDevelop 风格算子调用（伪代码级，含参数）
    QString name = node->name();
    QStringList calls;
    QString lower = name.toLower();

    if (lower.contains(QStringLiteral("read")) || lower.contains(QStringLiteral("readimage"))) {
        calls << QStringLiteral("read_image (Image, 'path')");
    } else if (lower.contains(QStringLiteral("threshold"))) {
        double minV = node->getParam(QStringLiteral("min")).toDouble();
        double maxV = node->getParam(QStringLiteral("max")).toDouble();
        calls << QStringLiteral("threshold (Image, Region, %1, %2)").arg(minV).arg(maxV);
    } else if (lower.contains(QStringLiteral("blur"))) {
        double sigma = node->getParam(QStringLiteral("sigma")).toDouble();
        calls << QStringLiteral("gauss_filter (Image, ImageBlurred, %1)").arg(sigma);
    } else if (lower.contains(QStringLiteral("dilate"))) {
        calls << QStringLiteral("dilation_circle (Region, RegionDilation, 3.5)");
    } else if (lower.contains(QStringLiteral("erode"))) {
        calls << QStringLiteral("erosion_circle (Region, RegionErosion, 3.5)");
    } else if (lower.contains(QStringLiteral("convert"))) {
        calls << QStringLiteral("convert_image_type (Image, ImageConverted, 'byte')");
    } else if (lower.contains(QStringLiteral("histogram"))) {
        calls << QStringLiteral("equ_histo_image (Image, ImageEqualized)");
    } else {
        calls << QStringLiteral("%1 (Image, Result)").arg(name);
    }

    QStringList indented;
    for (const QString &c : calls)
        indented << QStringLiteral("    ") + c;
    return indented.join(QLatin1Char('\n'));
}

void CodeExportDialog::generateCode(int flowIndex)
{
    if (flowIndex < 0 || flowIndex >= m_flows.size()) return;
    FlowScene *scene = m_flows[flowIndex];
    if (!scene) return;

    QString lang = m_langCombo->currentData().toString();
    QStringList lines;

    if (lang == QStringLiteral("cpp")) {
        lines << QStringLiteral("// \u5BFC\u51FA\u7684 C++ \u4EE3\u7801 (HALCON C++)");
        lines << QStringLiteral("#include <HalconCpp.h>");
        lines << QStringLiteral("using namespace HalconCpp;");
        lines << QStringLiteral("");
        lines << QStringLiteral("void RunFlow%1()").arg(flowIndex + 1);
        lines << QStringLiteral("{");
        lines << QStringLiteral("    HImage image;");
        lines << QStringLiteral("    HRegion region;");
        lines << QStringLiteral("    HTuple value;");
        for (NodeBase *node : scene->nodes()) {
            QString snippet = nodeToHalcon(node);
            if (!snippet.isEmpty())
                lines << snippet;
        }
        lines << QStringLiteral("}");
    } else {
        lines << QStringLiteral("* \u5BFC\u51FA\u7684 HDevelop \u4EE3\u7801");
        lines << QStringLiteral("dev_update_off ()");
        lines << QStringLiteral("");
        for (NodeBase *node : scene->nodes()) {
            QString snippet = nodeToHalcon(node);
            if (!snippet.isEmpty())
                lines << snippet;
        }
    }

    m_codeView->setPlainText(lines.join(QLatin1Char('\n')));
}

void CodeExportDialog::onCopy()
{
    QApplication::clipboard()->setText(m_codeView->toPlainText());
    QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
        QStringLiteral("\u4EE3\u7801\u5DF2\u590D\u5236\u5230\u526A\u8D34\u677F"));
}

void CodeExportDialog::onSave()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("\u4FDD\u5B58\u4EE3\u7801"), QString(),
        QStringLiteral("HDevelop (*.hdev);;C++ (*.cpp);;\u6587\u672C (*.txt)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << m_codeView->toPlainText();
        file.close();
        QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u5DF2\u4FDD\u5B58\u5230: %1").arg(path));
    } else {
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"),
            QStringLiteral("\u65E0\u6CD5\u5199\u5165\u6587\u4EF6"));
    }
}
