#include "HelpViewer.h"
#include "NodeBase.h"
#include "HalconNode.h"
#include "NodeRegistry.h"
#include "NodeResultFields.h"
#include "Port.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTextBrowser>
#include <QTextCursor>
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

    // 搜索（2026-11-12 审核）：QTextBrowser::find 从**当前光标**继续，逐字符输入时
    // 第二次查找会从上一次命中位置往后找，输入「参数」常找不到；空文本也不会清掉旧选区。
    // 每次输入都从头开始，找不到时回卷一次；空文本只清选区。
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        QTextCursor cursor = m_textBrowser->textCursor();
        cursor.clearSelection();
        cursor.movePosition(QTextCursor::Start);
        m_textBrowser->setTextCursor(cursor);
        if (text.isEmpty())
            return;
        if (!m_textBrowser->find(text)) {
            QTextCursor rewind = m_textBrowser->textCursor();
            rewind.movePosition(QTextCursor::Start);
            m_textBrowser->setTextCursor(rewind);
            m_textBrowser->find(text);
        }
    });
}

void HelpViewer::showHelpForNode(NodeBase *node)
{
    if (!node) return;
    // 换算子时重置搜索：旧关键字与新内容无关，留着会让人以为搜不到是内容缺失
    if (m_searchEdit) m_searchEdit->clear();

    // 帮助按“算子类型 ID”查注册表；moduleId 是运行期实例号（数字），查不到任何注册项。
    // 类型 ID 由 NodeRegistry 在创建节点时写入 vfpNodeTypeId；极老方案可能缺属性，退回类名。
    QString nodeId = node->property("vfpNodeTypeId").toString();
    if (nodeId.isEmpty())
        nodeId = QString::fromLatin1(node->metaObject()->className());
    m_currentNodeId = nodeId;

    // 优先按实例生成详细帮助（含真实参数值/范围/结果字段）；生成失败再退回按类型 ID 的通用内容。
    // 旧实现无论点哪个模块都只显示同一份模板文案（"参数在模块配置窗中…"），现场无法据此核对参数。
    const QString detailed = buildInstanceHelpHtml(node);
    if (!detailed.isEmpty()) {
        m_textBrowser->setHtml(detailed);
        const NodeRegistration *reg = NodeRegistry::instance().findById(nodeId);
        m_titleLabel->setText(QStringLiteral("算子帮助：%1")
                                  .arg(reg ? reg->displayName : node->name()));
        show();
        raise();
        activateWindow();
        return;
    }

    loadHelpContent(nodeId);
    show();
    raise();
    activateWindow();
}

void HelpViewer::showHelpForNodeId(const QString &nodeId)
{
    if (m_searchEdit) m_searchEdit->clear();
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
    const NodeRegistration *reg = NodeRegistry::instance().findById(nodeId);
    const QString title = QStringLiteral("算子帮助：%1")
                              .arg(reg ? reg->displayName : nodeId);

    // 查找已注册的帮助内容
    if (s_helpContents.contains(nodeId)) {
        m_textBrowser->setHtml(s_helpContents[nodeId]);
        m_titleLabel->setText(title);
        return;
    }

    // 使用默认帮助内容
    QString defaultContent = createDefaultHelpContent(nodeId);
    m_textBrowser->setHtml(defaultContent);
    m_titleLabel->setText(title);
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

    // 端口与参数说明按算子分组给出，避免数据节点也显示“图像输入/图像输出”
    const bool dataFlow = reg && (reg->group == QStringLiteral("逻辑与控制")
                                  || reg->group == QStringLiteral("结果输出"));
    const QString inputList = dataFlow
        ? QStringLiteral("<li><strong>数据输入</strong>：上游模块的结果数据</li>")
        : QStringLiteral("<li><strong>图像输入</strong>：待处理的图像数据</li>");
    const QString outputList = dataFlow
        ? QStringLiteral("<li><strong>数据输出</strong>：判断/统计/格式化后的结果</li>")
        : QStringLiteral("<li><strong>图像输出</strong>：处理后的图像数据</li>"
                         "<li><strong>结果数据</strong>：测量/定位结果</li>");

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
%6
</ul>

<h2>输出端口</h2>
<p>本算子提供以下输出：</p>
<ul>
%7
</ul>

<h2>参数说明</h2>
<p>参数在模块配置窗中按“基本参数 / 运行参数 / 结果显示”分组展示；
数值型运行参数支持“自定义 / 订阅”，左下角可恢复默认值。</p>

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
    .arg(description)
    .arg(inputList)
    .arg(outputList);

    return html;
}

QString HelpViewer::emptyListEntry()
{
    return QStringLiteral("<li>无</li>");
}

QString HelpViewer::enumValueText(const QStringList &values, int index)
{
    if (index >= 0 && index < values.size())
        return values.at(index);
    return QStringLiteral("（越界索引 %1）").arg(index);
}

QString HelpViewer::buildInstanceHelpHtml(NodeBase *node) const
{
    if (!node)
        return QString();

    auto *halcon = qobject_cast<HalconNode *>(node);
    const ParamSpecList specs = halcon ? halcon->paramSpecs() : ParamSpecList();
    const QVector<NodeResultField> fields = node->resultDisplayFields();
    if (specs.isEmpty() && fields.isEmpty())
        return QString();   // 非算法节点：回退到按类型 ID 的通用帮助

    const QString nodeId = node->property("vfpNodeTypeId").toString();
    const NodeRegistration *reg = NodeRegistry::instance().findById(nodeId);
    const QString displayName = reg ? reg->displayName : node->name();
    const QString group = reg ? reg->group : QStringLiteral("未分组");
    const QString description = (reg && !reg->description.isEmpty())
                                    ? reg->description
                                    : QStringLiteral("暂无描述");

    auto escape = [](const QString &text) { return text.toHtmlEscaped(); };

    QString html = QStringLiteral(R"(
<!DOCTYPE html><html><head><meta charset="utf-8">
<style>
body { font-family: "Microsoft YaHei", sans-serif; color:#e0e0e0; background:#2b2b3d; padding:16px; line-height:1.6; }
h1 { color:#1890ff; border-bottom:2px solid #1890ff; padding-bottom:8px; }
h2 { color:#52c41a; margin-top:20px; }
.info-box { background:#1e1e2e; border:1px solid #4a6a9c; border-radius:4px; padding:12px; margin:10px 0; }
.param-table { width:100%; border-collapse:collapse; margin:10px 0; }
.param-table th, .param-table td { border:1px solid #4a6a9c; padding:8px; text-align:left; vertical-align:top; }
.param-table th { background:#3a6ea5; color:white; }
.param-table tr:nth-child(even) { background:#1e1e2e; }
.note { background:#2a3a2a; border-left:4px solid #52c41a; padding:8px 12px; margin:10px 0; }
.warning { background:#3a2a2a; border-left:4px solid #ff4d4f; padding:8px 12px; margin:10px 0; }
</style></head><body>
)");
    html += QStringLiteral("<h1>%1</h1>").arg(escape(displayName));
    html += QStringLiteral("<div class=\"info-box\"><strong>节点ID：</strong>%1<br>"
                           "<strong>所属分组：</strong>%2<br>"
                           "<strong>实例名：</strong>%3</div>")
                .arg(escape(nodeId), escape(group), escape(node->name()));
    html += QStringLiteral("<h2>功能描述</h2><p>%1</p>").arg(escape(description));

    // 端口：按实例取，未连接的端口也列出，便于确认该连哪个
    html += QStringLiteral("<h2>端口</h2><p><strong>输入</strong></p><ul>");
    const QList<Port *> inputs = node->inputPorts();
    if (inputs.isEmpty())
        html += emptyListEntry();
    for (Port *port : inputs)
        html += QStringLiteral("<li>%1</li>").arg(escape(port->name()));
    // 订阅输入：逻辑槽不在画布上生成端口，不在帮助里列出来用户就不知道可以订阅
    const int logicalInputs = node->logicalInputPortCount();
    if (logicalInputs > inputs.size()) {
        html += QStringLiteral("</ul><p><strong>订阅输入（基本参数 → 输入订阅）</strong></p><ul>");
        for (int slot = inputs.size(); slot < logicalInputs; ++slot) {
            const QString slotName = node->inputSlotName(slot);
            if (!slotName.isEmpty())
                html += QStringLiteral("<li>%1</li>").arg(escape(slotName));
        }
    }
    html += QStringLiteral("</ul><p><strong>输出</strong></p><ul>");
    const QList<Port *> outputs = node->outputPorts();
    if (outputs.isEmpty())
        html += emptyListEntry();
    for (Port *port : outputs)
        html += QStringLiteral("<li>%1</li>").arg(escape(port->name()));
    html += QStringLiteral("</ul>");

    // 参数表：当前值 / 范围与默认 / 说明（说明来自 ParamSpec.tooltip，与参数面板 tooltip 同源）
    if (halcon && !specs.isEmpty()) {
        html += QStringLiteral("<h2>参数说明</h2>"
                               "<table class=\"param-table\"><tr><th>参数</th><th>当前值</th>"
                               "<th>范围 / 默认</th><th>说明</th></tr>");
        for (const ParamSpec &spec : specs) {
            const QString label = spec.label.isEmpty() ? spec.name : spec.label;
            QString current = halcon->getParam(spec.name).toString();
            if (spec.type == ParamType::Enum && !spec.enumValues.isEmpty()) {
                const int idx = halcon->getParam(spec.name).toInt();
                current = enumValueText(spec.enumValues, idx);
            }
            if (!spec.unit.isEmpty() && !current.isEmpty())
                current += QLatin1Char(' ') + spec.unit;

            QString range;
            if (spec.hasRange) {
                range = QStringLiteral("%1 ~ %2").arg(spec.minValue.toString(),
                                                      spec.maxValue.toString());
            }
            const QString def = spec.defaultValue.toString();
            if (!def.isEmpty())
                range += range.isEmpty() ? QStringLiteral("默认 %1").arg(def)
                                         : QStringLiteral("，默认 %1").arg(def);
            if (range.isEmpty())
                range = QStringLiteral("—");

            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                        .arg(escape(label), escape(current), escape(range),
                             escape(spec.tooltip));
        }
        html += QStringLiteral("</table>");
    }

    // 结果字段：与「模块结果」页签同源
    if (!fields.isEmpty()) {
        html += QStringLiteral("<h2>结果字段</h2><table class=\"param-table\">"
                               "<tr><th>名称</th><th>键</th><th>单位</th><th>说明</th></tr>");
        for (const NodeResultField &field : fields) {
            QString note = field.group.isEmpty() ? QStringLiteral("模块级")
                                                 : QStringLiteral("逐项：%1").arg(field.group);
            if (field.kind == NodeResultField::Status)
                note += QStringLiteral("（OK/NG）");
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>")
                        .arg(escape(field.label), escape(field.key),
                             escape(field.unit), escape(note));
        }
        html += QStringLiteral("</table>");
    }

    html += QStringLiteral(
        "<h2>使用提示</h2><div class=\"note\">参数在模块配置窗中修改后立即生效；"
        "数值型运行参数支持“自定义 / 订阅”，左下角可恢复默认值。</div>"
        "<h2>注意事项</h2><div class=\"warning\">参数表中的“当前值”是打开本帮助时的快照；"
        "若在配置窗改过参数，请重新打开帮助核对。按 F1 可打开完整使用手册。</div>"
        "</body></html>");
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
