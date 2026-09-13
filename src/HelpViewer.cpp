#include "HelpViewer.h"
#include "NodeBase.h"
#include "NodeRegistry.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTextBrowser>
#include <QDialogButtonBox>

// 静态帮助文档内容缓存
QMap<QString, QString> HelpViewer::s_helpContents;

HelpViewer::HelpViewer(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("算子帮助文档"));
    setMinimumSize(600, 500);
    resize(700, 600);

    // 设置样式
    setStyleSheet(
        "QDialog { background-color: #1e1e2e; }"
        "QTextBrowser { background-color: #2b2b3d; color: #e0e0e0; border: 1px solid #4a6a9c; "
        "  border-radius: 4px; padding: 8px; font-size: 14px; }"
        "QLineEdit { background-color: #2b2b3d; color: #e0e0e0; "
        "  border: 1px solid #4a6a9c; border-radius: 4px; padding: 4px 8px; font-size: 13px; }"
        "QPushButton { background-color: #3a6ea5; color: white; border: none; "
        "  border-radius: 4px; padding: 6px 16px; font-size: 13px; }"
        "QPushButton:hover { background-color: #4a7eb5; }"
        "QPushButton:pressed { background-color: #2a5e95; }"
        "QLabel { color: #e0e0e0; font-size: 14px; }"
    );

    // 创建布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 标题栏
    QHBoxLayout *titleLayout = new QHBoxLayout();
    m_titleLabel = new QLabel(QStringLiteral("算子帮助文档"));
    m_titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #1890ff;");
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();

    // 搜索框
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索帮助内容..."));
    m_searchEdit->setFixedWidth(200);
    titleLayout->addWidget(m_searchEdit);

    mainLayout->addLayout(titleLayout);

    // 内容浏览器
    m_textBrowser = new QTextBrowser();
    m_textBrowser->setOpenExternalLinks(true);
    mainLayout->addWidget(m_textBrowser);

    // 底部按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton *closeButton = new QPushButton(QStringLiteral("关闭"));
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeButton);

    mainLayout->addLayout(buttonLayout);

    // 连接搜索信号
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!text.isEmpty()) {
            m_textBrowser->find(text);
        }
    });
}

void HelpViewer::showHelpForNode(NodeBase *node)
{
    if (!node) return;

    QString nodeId = QString::number(node->moduleId());
    showHelpForNodeId(nodeId);
}

void HelpViewer::showHelpForNodeId(const QString &nodeId)
{
    m_currentNodeId = nodeId;
    loadHelpContent(nodeId);
    show();
    raise();
    activateWindow();
}

void HelpViewer::registerHelpContent(const QString &nodeId, const QString &htmlContent)
{
    s_helpContents[nodeId] = htmlContent;
}

void HelpViewer::loadHelpContent(const QString &nodeId)
{
    // 查找已注册的帮助内容
    if (s_helpContents.contains(nodeId)) {
        m_textBrowser->setHtml(s_helpContents[nodeId]);
        m_titleLabel->setText(QStringLiteral("算子帮助：%1").arg(nodeId));
        return;
    }

    // 使用默认帮助内容
    QString defaultContent = createDefaultHelpContent(nodeId);
    m_textBrowser->setHtml(defaultContent);
    m_titleLabel->setText(QStringLiteral("算子帮助：%1").arg(nodeId));
}

QString HelpViewer::createDefaultHelpContent(const QString &nodeId) const
{
    // 查找节点注册信息
    auto &registry = NodeRegistry::instance();
    const NodeRegistration *reg = registry.findById(nodeId);

    QString displayName = reg ? reg->displayName : nodeId;
    QString description = reg ? reg->description : QString();
    QString group = reg ? reg->group : QStringLiteral("未知分组");
    if (description.isEmpty()) {
        if (nodeId == QStringLiteral("DeepOcrNode"))
            description = QStringLiteral("HALCON DeepOCR：识别文字。需要 HALCON 运行时与模型。");
        else if (nodeId == QStringLiteral("ImageReadNode")
                 || nodeId == QStringLiteral("HalconImageSourceNode"))
            description = QStringLiteral("HALCON 图像层（读图/采集）。日常算法走 OpenCV。");
        else if (nodeId.startsWith(QStringLiteral("Opencv"))
                 || nodeId == QStringLiteral("DnnInferNode")
                 || nodeId == QStringLiteral("TesseractOcrNode")
                 || nodeId == QStringLiteral("ZxingBarcodeNode"))
            description = QStringLiteral("图像算法由 OpenCV 或开源库实现。HALCON 仅作为图容器。");
        else
            description = QStringLiteral("暂无描述");
    }

    // 生成帮助文档HTML
    QString html = QStringLiteral(R"(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
body {
    font-family: "Microsoft YaHei", "微软雅黑", sans-serif;
    color: #e0e0e0;
    background-color: #2b2b3d;
    padding: 16px;
    line-height: 1.6;
}
h1 {
    color: #1890ff;
    border-bottom: 2px solid #1890ff;
    padding-bottom: 8px;
}
h2 {
    color: #52c41a;
    margin-top: 20px;
}
.info-box {
    background-color: #1e1e2e;
    border: 1px solid #4a6a9c;
    border-radius: 4px;
    padding: 12px;
    margin: 10px 0;
}
.param-table {
    width: 100%;
    border-collapse: collapse;
    margin: 10px 0;
}
.param-table th, .param-table td {
    border: 1px solid #4a6a9c;
    padding: 8px;
    text-align: left;
}
.param-table th {
    background-color: #3a6ea5;
    color: white;
}
.param-table tr:nth-child(even) {
    background-color: #1e1e2e;
}
.note {
    background-color: #2a3a2a;
    border-left: 4px solid #52c41a;
    padding: 8px 12px;
    margin: 10px 0;
}
.warning {
    background-color: #3a2a2a;
    border-left: 4px solid #ff4d4f;
    padding: 8px 12px;
    margin: 10px 0;
}
</style>
</head>
<body>
<h1>%1</h1>
<div class="info-box">
<strong>节点ID：</strong>%2<br>
<strong>所属分组：</strong>%3<br>
<strong>节点类型：</strong>%4
</div>

<h2>功能描述</h2>
<p>%5</p>

<h2>输入端口</h2>
<p>本算子接受以下输入：</p>
<ul>
<li><strong>图像输入</strong>：待处理的图像数据</li>
</ul>

<h2>输出端口</h2>
<p>本算子提供以下输出：</p>
<ul>
<li><strong>图像输出</strong>：处理后的图像数据</li>
<li><strong>结果数据</strong>：分析结果或参数</li>
</ul>

<h2>参数说明</h2>
<p>本算子的参数可在右侧参数面板中配置。具体参数取决于算子实现。</p>

<h2>使用示例</h2>
<div class="note">
<strong>提示：</strong>将本算子拖放到流程编辑区，连接输入输出端口，然后配置参数即可使用。
</div>

<h2>注意事项</h2>
<div class="warning">
<strong>注意：</strong>请确保输入数据格式正确，否则可能导致处理失败。
</div>

<h2>相关算子</h2>
<p>本算子常与以下算子配合使用：</p>
<ul>
<li>图像采集算子</li>
<li>图像预处理算子</li>
<li>结果显示算子</li>
</ul>
</body>
</html>
)")
    .arg(displayName)
    .arg(nodeId)
    .arg(group)
    .arg(getNodeCategoryDescription(nodeId))
    .arg(description);

    return html;
}

QString HelpViewer::getNodeCategoryDescription(const QString &nodeId) const
{
    // 根据节点ID判断类型
    if (nodeId.contains("ImageSource") || nodeId.contains("Camera") || nodeId.contains("Read")) {
        return QStringLiteral("图像源");
    } else if (nodeId.contains("Threshold") || nodeId.contains("Filter") || nodeId.contains("Enhance")) {
        return QStringLiteral("图像处理");
    } else if (nodeId.contains("Blob") || nodeId.contains("Shape") || nodeId.contains("Match")) {
        return QStringLiteral("形状分析");
    } else if (nodeId.contains("Caliper") || nodeId.contains("Measure") || nodeId.contains("Line")) {
        return QStringLiteral("卡尺测量");
    } else if (nodeId.contains("Comm") || nodeId.contains("TCP") || nodeId.contains("Serial")) {
        return QStringLiteral("通信模块");
    } else if (nodeId.contains("Display") || nodeId.contains("Save") || nodeId.contains("Output")) {
        return QStringLiteral("输出显示");
    }
    return QStringLiteral("通用算子");
}
