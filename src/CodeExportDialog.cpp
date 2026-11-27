#include "CodeExportDialog.h"
#include "FlowScene.h"
#include "NodeBase.h"
#include "Port.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QSet>
#include "ExportWriteCheck.h"

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
    // Z2：切换语言必须立即重生成，否则复制/保存拿到的是上一语言的旧代码
    connect(m_langCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CodeExportDialog::onGenerate);
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

QString CodeExportDialog::nodeToSnippet(NodeBase *node, const QString &lang) const
{
    if (!node) return QString();
    // Z2：旧实现按中文显示名猜 HALCON 算子，79 个现役节点没有一个命中关键字，
    // 兜底还会把中文显示名当成算子名输出这种根本不存在的标识符；即使命中，
    // gauss_filter/equ_histo_image 等也只存在停用节点里、交付包没有运行时支撑。
    // 这里改为按 Qt 元对象类型名 + 真实参数输出结构参考，绝不伪造可执行算子名。
    const QString prefix = (lang == QStringLiteral("cpp")) ? QStringLiteral("// ")
                                                           : QStringLiteral("* ");
    const QString typeName = QString::fromLatin1(node->metaObject()->className());
    QStringList lines;
    lines << prefix + QStringLiteral("节点 %1 ｜ 类型 %2 ｜ 模块号 %3")
                          .arg(node->name()).arg(typeName).arg(node->moduleId());

    // 框架字段不是算法参数，导出会误导；只列节点自己声明的参数及当前值
    static const QSet<QString> kFrameworkKeys = {
        QStringLiteral("moduleStatus"), QStringLiteral("moduleName"),
        QStringLiteral("lastError"), QStringLiteral("elapsedMs"),
        QStringLiteral("resultNg"), QStringLiteral("calibNote")
    };
    QStringList pairs;
    const QList<QString> names = node->getAllParamNames();
    for (const QString &key : names) {
        if (kFrameworkKeys.contains(key))
            continue;
        pairs << QStringLiteral("%1=%2").arg(key, node->getParam(key).toString());
    }
    lines << prefix + QStringLiteral("参数: %1")
                          .arg(pairs.isEmpty() ? QStringLiteral("（无）") : pairs.join(QStringLiteral(", ")));

    QStringList outputs;
    for (Port *port : node->outputPorts())
        if (port) outputs << port->name();
    if (!outputs.isEmpty())
        lines << prefix + QStringLiteral("输出: %1").arg(outputs.join(QStringLiteral(", ")));

    return lines.join(QLatin1Char('\n'));
}

void CodeExportDialog::generateCode(int flowIndex)
{
    if (flowIndex < 0 || flowIndex >= m_flows.size()) return;
    FlowScene *scene = m_flows[flowIndex];
    if (!scene) return;

    QString lang = m_langCombo->currentData().toString();
    QStringList lines;

    if (lang == QStringLiteral("cpp")) {
        lines << QStringLiteral("// flow structure reference (not compilable operator code)");
        lines << QStringLiteral("// node type names and current parameters; configure operators in the app");
        lines << QStringLiteral("");
        lines << QStringLiteral("void RunFlow%1()").arg(flowIndex + 1);
        lines << QStringLiteral("{");
        for (NodeBase *node : scene->nodes()) {
            const QString snippet = nodeToSnippet(node, lang);
            if (!snippet.isEmpty())
                lines << snippet;
        }
        lines << QStringLiteral("}");
    } else {
        lines << QStringLiteral("* flow structure reference (not runnable HDevelop program)");
        lines << QStringLiteral("* node type names and current parameters; configure operators in the app");
        lines << QStringLiteral("");
        for (NodeBase *node : scene->nodes()) {
            const QString snippet = nodeToSnippet(node, lang);
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
    // Z2: save filter and default suffix must follow the selected language
    const bool cpp = (m_langCombo->currentData().toString() == QStringLiteral("cpp"));
    QFileDialog dlg(this, QStringLiteral("\u4FDD\u5B58\u4EE3\u7801"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setDefaultSuffix(cpp ? QStringLiteral("cpp") : QStringLiteral("hdev"));
    dlg.setNameFilter(cpp ? QStringLiteral("C++ (*.cpp);;\u6587\u672C (*.txt)")
                           : QStringLiteral("HDevelop (*.hdev);;\u6587\u672C (*.txt)"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QStringList chosen = dlg.selectedFiles();
    if (chosen.isEmpty())
        return;
    const QString path = chosen.first();
    if (path.isEmpty()) return;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << m_codeView->toPlainText();
        // 写成功才算导出成功（2026-10-26）：open() 成功不代表写完成功，收尾检查集中在
        // ExportWriteCheck（磁盘满/网络盘断开只在 flush 或 close 时暴露）。
        if (!ExportWriteCheck::finishTextExport(this, file, out, path))
            return;
        QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u5DF2\u4FDD\u5B58\u5230: %1").arg(path));
    } else {
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"),
            QStringLiteral("\u65E0\u6CD5\u5199\u5165\u6587\u4EF6"));
    }
}
