#include "FormulaNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <cmath>

// ---------- 简单表达式求值器（递归下降） ----------
// 支持：数字、p0/p1.../pi/e、+ - * / ( )、一元负号、函数 abs/min/max/sqrt/pow/round/floor/ceil/sin/cos/tan
namespace {

class ExprParser
{
public:
    ExprParser(const QString &s, const QMap<QString, double> &vars)
        : m_s(s), m_vars(vars), m_pos(0) {}

    bool parse(double &result)
    {
        skipWs();
        double v = 0;
        if (!parseAddSub(v) || m_pos < m_s.size()) {
            return false;
        }
        result = v;
        return true;
    }

private:
    void skipWs()
    {
        while (m_pos < m_s.size() && m_s[m_pos].isSpace()) ++m_pos;
    }

    bool peekChar(QChar c) const
    {
        return m_pos < m_s.size() && m_s[m_pos] == c;
    }

    bool parseAddSub(double &out)
    {
        double lhs = 0;
        if (!parseMulDiv(lhs)) return false;
        while (true) {
            skipWs();
            if (peekChar('+')) {
                ++m_pos;
                double rhs = 0;
                if (!parseMulDiv(rhs)) return false;
                lhs += rhs;
            } else if (peekChar('-')) {
                ++m_pos;
                double rhs = 0;
                if (!parseMulDiv(rhs)) return false;
                lhs -= rhs;
            } else {
                break;
            }
        }
        out = lhs;
        return true;
    }

    bool parseMulDiv(double &out)
    {
        double lhs = 0;
        if (!parseUnary(lhs)) return false;
        while (true) {
            skipWs();
            if (peekChar('*')) {
                ++m_pos;
                double rhs = 0;
                if (!parseUnary(rhs)) return false;
                lhs *= rhs;
            } else if (peekChar('/')) {
                ++m_pos;
                double rhs = 0;
                if (!parseUnary(rhs)) return false;
                if (std::abs(rhs) < 1e-12) return false; // 除零
                lhs /= rhs;
            } else {
                break;
            }
        }
        out = lhs;
        return true;
    }

    bool parseUnary(double &out)
    {
        skipWs();
        if (peekChar('-')) {
            ++m_pos;
            double v = 0;
            if (!parseUnary(v)) return false;
            out = -v;
            return true;
        }
        if (peekChar('+')) {
            ++m_pos;
            return parseUnary(out);
        }
        return parsePrimary(out);
    }

    bool parsePrimary(double &out)
    {
        skipWs();
        if (m_pos >= m_s.size()) return false;

        QChar c = m_s[m_pos];
        // 括号
        if (c == '(') {
            ++m_pos;
            double v = 0;
            if (!parseAddSub(v)) return false;
            skipWs();
            if (!peekChar(')')) return false;
            ++m_pos;
            out = v;
            return true;
        }
        // 数字
        if (c.isDigit() || c == '.') {
            QRegularExpression re(QStringLiteral(R"(^\d*\.?\d+(?:[eE][+-]?\d+)?)"));
            QRegularExpressionMatch m = re.match(m_s.mid(m_pos));
            if (!m.hasMatch()) return false;
            bool ok = false;
            double v = m.captured(0).toDouble(&ok);
            if (!ok) return false;
            m_pos += m.capturedLength();
            out = v;
            return true;
        }
        // 标识符：函数或变量
        if (c.isLetter() || c == '_') {
            QRegularExpression re(QStringLiteral(R"(^[A-Za-z_][A-Za-z0-9_]*)"));
            QRegularExpressionMatch m = re.match(m_s.mid(m_pos));
            if (!m.hasMatch()) return false;
            QString ident = m.captured(0);
            m_pos += ident.size();

            skipWs();
            // 函数调用
            if (peekChar('(')) {
                ++m_pos;
                QList<double> args;
                while (true) {
                    skipWs();
                    if (peekChar(')')) {
                        ++m_pos;
                        break;
                    }
                    double a = 0;
                    if (!parseAddSub(a)) return false;
                    args.append(a);
                    skipWs();
                    if (peekChar(',')) { ++m_pos; continue; }
                    if (peekChar(')')) { ++m_pos; break; }
                    return false;
                }
                return applyFunction(ident, args, out);
            }

            // 变量/常量
            if (ident.compare(QStringLiteral("pi"), Qt::CaseInsensitive) == 0) {
                out = 3.14159265358979323846;
                return true;
            }
            if (ident.compare(QStringLiteral("e"), Qt::CaseInsensitive) == 0) {
                out = 2.718281828459045;
                return true;
            }
            if (m_vars.contains(ident)) {
                out = m_vars.value(ident);
                return true;
            }
            return false; // 未定义变量
        }
        return false;
    }

    bool applyFunction(const QString &name, const QList<double> &args, double &out)
    {
        QString n = name.toLower();
        auto need = [&](int k) { return args.size() == k; };
        if (n == QStringLiteral("abs") && need(1))      { out = std::fabs(args[0]); return true; }
        if (n == QStringLiteral("sqrt") && need(1))     { if (args[0] < 0) return false; out = std::sqrt(args[0]); return true; }
        if (n == QStringLiteral("round") && need(1))    { out = std::round(args[0]); return true; }
        if (n == QStringLiteral("floor") && need(1))    { out = std::floor(args[0]); return true; }
        if (n == QStringLiteral("ceil") && need(1))     { out = std::ceil(args[0]); return true; }
        if (n == QStringLiteral("sin") && need(1))      { out = std::sin(args[0]); return true; }
        if (n == QStringLiteral("cos") && need(1))      { out = std::cos(args[0]); return true; }
        if (n == QStringLiteral("tan") && need(1))      { out = std::tan(args[0]); return true; }
        if (n == QStringLiteral("min") && need(2))      { out = qMin(args[0], args[1]); return true; }
        if (n == QStringLiteral("max") && need(2))      { out = qMax(args[0], args[1]); return true; }
        if (n == QStringLiteral("pow") && need(2))      { out = std::pow(args[0], args[1]); return true; }
        return false;
    }

    const QString &m_s;
    const QMap<QString, double> &m_vars;
    int m_pos;
};

} // namespace

FormulaNode::FormulaNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u516C\u5F0F\u8BA1\u7B97"));
    m_type = LOGIC;
}

void FormulaNode::init()
{
    addInputPort(QStringLiteral("p0"), PortDataType::Number);
    addInputPort(QStringLiteral("p1"), PortDataType::Number);
    addInputPort(QStringLiteral("p2"), PortDataType::Number);
    addInputPort(QStringLiteral("p3"), PortDataType::Number);
    addOutputPort(QStringLiteral("\u8BA1\u7B97\u7ED3\u679C"), PortDataType::Number);

    m_params[QStringLiteral("expression")] = QStringLiteral("p0 + p1");
}

bool FormulaNode::process()
{
    run();
    return true;
}

bool FormulaNode::evaluate(const QString &expression,
                           const QMap<QString, double> &variables,
                           double &result)
{
    ExprParser parser(expression, variables);
    return parser.parse(result);
}

void FormulaNode::run(bool /*autoSwitch*/)
{
    // 收集输入端口数值 → 变量表
    QMap<QString, double> vars;
    for (int i = 0; i < 4; ++i) {
        auto data = getInputData(i);
        if (data) {
            bool ok = false;
            double v = data->getData().toDouble(&ok);
            if (ok) vars[QStringLiteral("p%1").arg(i)] = v;
        }
    }

    double result = 0.0;
    if (evaluate(getParam(QStringLiteral("expression")).toString(), vars, result)) {
        auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::Number, QVariant(result));
        setOutputData(0, obj);
    } else {
        // 求值失败：清空输出，避免下游误用上一轮结果（P2）
        setOutputData(0, QSharedPointer<DataObject>());
    }
}

void FormulaNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类加锁 + 校验），不再维护无锁成员镜像
    HalconNode::setParam(name, value);
}

QWidget *FormulaNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u516C\u5F0F\u8BA1\u7B97</b>")));

    auto *info = new QLabel(QStringLiteral(
        "\u652F\u6301\u8F93\u5165\u53E3 p0\uff5ep3 \u5F15\u7528\u3002\n"
        "\u793A\u4F8B: (p0 + p1) * 2 / max(p2, 1)\n"
        "\u5E38\u91CF: pi, e; \u51FD\u6570: abs/sqrt/round/floor/ceil/min/max/pow/sin/cos/tan"));
    info->setWordWrap(true);
    info->setStyleSheet("color:gray; font-size:11px;");
    layout->addWidget(info);

    layout->addWidget(new QLabel(QStringLiteral("\u8868\u8FBE\u5F0F:")));
    m_expressionEdit = new QTextEdit();
    m_expressionEdit->setObjectName(QStringLiteral("formulaExpression"));
    m_expressionEdit->setPlainText(getParam(QStringLiteral("expression")).toString());
    m_expressionEdit->setMinimumHeight(70);
    layout->addWidget(m_expressionEdit);

    connect(m_expressionEdit, &QTextEdit::textChanged, this, [this]() {
        setParam(QStringLiteral("expression"), m_expressionEdit->toPlainText());
    });

    layout->addStretch();
    return panel;
}

void FormulaNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *edit = panel->findChild<QTextEdit *>(QStringLiteral("formulaExpression"))) {
        QSignalBlocker b(edit);
        edit->setPlainText(getParam(QStringLiteral("expression")).toString());
    }
}

QJsonObject FormulaNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值取自参数表（唯一来源）
    obj[QStringLiteral("expression")] = QJsonValue::fromVariant(getParam(QStringLiteral("expression")));
    return obj;
}

void FormulaNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // expression 由基类从 params 恢复（唯一来源）
    // 兼容更早方案：expression 曾只存在顶层（无 params 段）。
    // 与旧实现逐字对齐：旧代码是 `m_expression = json["expression"].toString(m_expression)`，
    // 即**缺键时保留原值**（不是清空）——故这里保留 contains 守卫。
    if (!json.contains(QStringLiteral("params"))
        && json.contains(QStringLiteral("expression"))) {
        setParam(QStringLiteral("expression"), json.value(QStringLiteral("expression")).toVariant());
    }
}
