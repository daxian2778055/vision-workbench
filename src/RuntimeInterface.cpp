#include "RuntimeInterface.h"
#include <QSaveFile>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

QString runtimeControlTypeName(RuntimeControlType type)
{
    switch (type) {
    case RuntimeControlType::ImageView:    return QStringLiteral("图像显示");
    case RuntimeControlType::ValueDisplay: return QStringLiteral("数值显示");
    case RuntimeControlType::TextLabel:    return QStringLiteral("文本标签");
    case RuntimeControlType::StatusLight:  return QStringLiteral("状态灯");
    case RuntimeControlType::Button:       return QStringLiteral("按钮");
    case RuntimeControlType::ResultTable:  return QStringLiteral("结果表格");
    case RuntimeControlType::IoStatus:     return QStringLiteral("IO状态");
    case RuntimeControlType::ParamSetting: return QStringLiteral("参数设置");
    case RuntimeControlType::SwitchControl: return QStringLiteral("开关控制");
    case RuntimeControlType::ComboBox: return QStringLiteral("组合框");
    case RuntimeControlType::ImageAsset: return QStringLiteral("图片");
    case RuntimeControlType::WorldClock: return QStringLiteral("世界时钟");
    }
    return QStringLiteral("未知");
}

bool runtimeControlTypeFromName(const QString &name, RuntimeControlType &out)
{
    if (name == QStringLiteral("图像显示")) { out = RuntimeControlType::ImageView; return true; }
    if (name == QStringLiteral("数值显示")) { out = RuntimeControlType::ValueDisplay; return true; }
    if (name == QStringLiteral("文本标签")) { out = RuntimeControlType::TextLabel; return true; }
    if (name == QStringLiteral("状态灯"))   { out = RuntimeControlType::StatusLight; return true; }
    if (name == QStringLiteral("按钮"))     { out = RuntimeControlType::Button; return true; }
    if (name == QStringLiteral("结果表格")) { out = RuntimeControlType::ResultTable; return true; }
    if (name == QStringLiteral("IO状态"))   { out = RuntimeControlType::IoStatus; return true; }
    if (name == QStringLiteral("参数设置")) { out = RuntimeControlType::ParamSetting; return true; }
    if (name == QStringLiteral("开关控制")) { out = RuntimeControlType::SwitchControl; return true; }
    if (name == QStringLiteral("组合框"))   { out = RuntimeControlType::ComboBox; return true; }
    if (name == QStringLiteral("图片"))     { out = RuntimeControlType::ImageAsset; return true; }
    if (name == QStringLiteral("世界时钟")) { out = RuntimeControlType::WorldClock; return true; }
    return false;
}

bool parseParamActionTarget(const QString &method, const QString &param,
                            QString *nodeName, QString *paramKey, QVariant *value)
{
    const QString node = method.trimmed();
    if (node.isEmpty()) return false;
    const int eq = param.indexOf(QLatin1Char('='));
    if (eq <= 0) return false;          // 没有 '=' 或参数键为空
    const QString key = param.left(eq).trimmed();
    if (key.isEmpty()) return false;
    const QString raw = param.mid(eq + 1).trimmed();

    bool ok = false;
    const double asNum = raw.toDouble(&ok);
    if (nodeName) *nodeName = node;
    if (paramKey) *paramKey = key;
    if (value) *value = ok ? QVariant(asNum) : QVariant(raw);
    return true;
}

// ==================== RuntimeControl JSON ====================

static QJsonObject controlToJson(const RuntimeControl &c)
{
    QJsonObject o;
    o["type"] = runtimeControlTypeName(c.type);
    o["title"] = c.title;
    o["bindType"] = c.bindType;
    o["bindKey"] = c.bindKey;
    o["color"] = c.color;
    o["fontSize"] = c.fontSize;
    // 外观扩展（2026-11-09）：只在非默认时写出，旧布局文件不膨胀
    if (c.bold) o["bold"] = true;
    if (!c.bgColor.isEmpty()) o["bgColor"] = c.bgColor;
    if (!c.borderColor.isEmpty()) o["borderColor"] = c.borderColor;
    if (!c.pressedColor.isEmpty()) o["pressedColor"] = c.pressedColor;
    o["visible"] = c.visible;
    QJsonObject geo;
    geo["x"] = c.geometry.x();
    geo["y"] = c.geometry.y();
    geo["w"] = c.geometry.width();
    geo["h"] = c.geometry.height();
    o["geometry"] = geo;
    // 参数设置 / 开关控制 / 组合框都靠 paramKey 定位节点参数
    if (c.type == RuntimeControlType::ParamSetting
        || c.type == RuntimeControlType::SwitchControl
        || c.type == RuntimeControlType::ComboBox) {
        if (!c.paramKey.isEmpty()) o["paramKey"] = c.paramKey;
    }
    if (c.type == RuntimeControlType::ImageAsset && !c.imagePath.isEmpty())
        o["imagePath"] = c.imagePath;
    if (c.type == RuntimeControlType::WorldClock)
        o["timeZoneOffsetMinutes"] = c.timeZoneOffsetMinutes;
    if (c.type == RuntimeControlType::ComboBox && !c.comboOptions.isEmpty()) {
        QJsonArray opts;
        for (const QString &s : c.comboOptions) opts.append(s);
        o["comboOptions"] = opts;
    }
    if (c.type == RuntimeControlType::ParamSetting) {
        o["paramMin"] = c.paramMin;
        o["paramMax"] = c.paramMax;
    }
    // 按钮动作表：只在有内容时写出，旧方案不膨胀
    if (c.type == RuntimeControlType::Button && !c.actions.isEmpty()) {
        QJsonArray acts;
        for (const RuntimeAction &a : c.actions) {
            QJsonObject ao;
            ao["commandType"] = a.commandType;
            ao["method"] = a.method;
            ao["param"] = a.param;
            acts.append(ao);
        }
        o["actions"] = acts;
    }
    if (c.type == RuntimeControlType::ResultTable) {
        QJsonArray cols;
        for (const ResultColumn &col : c.columns) {
            QJsonObject co;
            co["header"] = col.header;
            co["bindType"] = col.bindType;
            co["bindKey"] = col.bindKey;
            cols.append(co);
        }
        o["columns"] = cols;
        o["maxRows"] = c.maxRows;
    }
    return o;
}

static RuntimeControl controlFromJson(const QJsonObject &o, RuntimeControlType type)
{
    RuntimeControl ctrl;
    ctrl.type = type;
    ctrl.title = o["title"].toString();
    ctrl.bindType = o["bindType"].toString();
    ctrl.bindKey = o["bindKey"].toString();
    ctrl.color = o["color"].toString(QStringLiteral("#3a6ea5"));
    ctrl.fontSize = o["fontSize"].toInt(16);
    // 外观扩展：旧布局没有这些键，默认值 = 不绘制/不加粗 ⇒ 外观与旧版一致
    ctrl.bold = o["bold"].toBool(false);
    ctrl.bgColor = o["bgColor"].toString();
    ctrl.borderColor = o["borderColor"].toString();
    ctrl.pressedColor = o["pressedColor"].toString();
    ctrl.visible = o["visible"].toBool(true);

    const QJsonObject geo = o["geometry"].toObject();
    ctrl.geometry = QRect(geo["x"].toInt(20), geo["y"].toInt(20),
                          geo["w"].toInt(240), geo["h"].toInt(160));

    if (ctrl.type == RuntimeControlType::Button) {
        const QJsonArray acts = o["actions"].toArray();
        for (const QJsonValue &av : acts) {
            const QJsonObject ao = av.toObject();
            RuntimeAction a;
            a.commandType = ao["commandType"].toString();
            a.method = ao["method"].toString();
            a.param = ao["param"].toString();
            if (a.commandType.isEmpty()) continue;
            ctrl.actions.append(a);
        }
    }

    if (ctrl.type == RuntimeControlType::ParamSetting
        || ctrl.type == RuntimeControlType::SwitchControl
        || ctrl.type == RuntimeControlType::ComboBox) {
        ctrl.paramKey = o["paramKey"].toString();
    }
    if (ctrl.type == RuntimeControlType::ComboBox) {
        const QJsonArray opts = o["comboOptions"].toArray();
        for (const QJsonValue &v : opts) ctrl.comboOptions << v.toString();
    }
    if (ctrl.type == RuntimeControlType::ImageAsset)
        ctrl.imagePath = o["imagePath"].toString();
    if (ctrl.type == RuntimeControlType::WorldClock)
        ctrl.timeZoneOffsetMinutes = o["timeZoneOffsetMinutes"].toInt(480);
    if (ctrl.type == RuntimeControlType::ParamSetting) {
        ctrl.paramMin = o["paramMin"].toDouble(0.0);
        ctrl.paramMax = o["paramMax"].toDouble(100.0);
        if (ctrl.paramMax <= ctrl.paramMin) ctrl.paramMax = ctrl.paramMin + 1.0;
    }

    if (ctrl.type == RuntimeControlType::ResultTable) {
        const QJsonArray cols = o["columns"].toArray();
        for (const QJsonValue &cv : cols) {
            const QJsonObject co = cv.toObject();
            ResultColumn col;
            col.header = co["header"].toString();
            col.bindType = co["bindType"].toString(QStringLiteral("global"));
            col.bindKey = co["bindKey"].toString();
            ctrl.columns.append(col);
        }
        ctrl.maxRows = o["maxRows"].toInt(100);
        if (ctrl.maxRows < 1) ctrl.maxRows = 1;
    }
    return ctrl;
}

// ==================== RuntimeInterfacePage ====================

RuntimeControl *RuntimeInterfacePage::addControl(RuntimeControlType type, const QRect &geo)
{
    RuntimeControl ctrl;
    ctrl.type = type;
    ctrl.title = ctrl.defaultTitle();
    ctrl.geometry = geo;
    controls.append(ctrl);
    return &controls.last();
}

void RuntimeInterfacePage::removeControl(int index)
{
    if (index >= 0 && index < controls.size())
        controls.removeAt(index);
}

void RuntimeInterfacePage::clear()
{
    controls.clear();
}

int RuntimeInterfacePage::indexOf(const RuntimeControl *ctrl) const
{
    for (int i = 0; i < controls.size(); ++i) {
        if (&controls[i] == ctrl) return i;
    }
    return -1;
}

QJsonObject RuntimeInterfacePage::toJson() const
{
    QJsonObject root;
    root["pageName"] = pageName;
    QJsonArray arr;
    for (const RuntimeControl &c : controls)
        arr.append(controlToJson(c));
    root["controls"] = arr;
    return root;
}

bool RuntimeInterfacePage::fromJson(const QJsonObject &json)
{
    if (!json.contains("controls")) return false;
    controls.clear();
    pageName = json["pageName"].toString(QStringLiteral("页面1"));
    const QJsonArray arr = json["controls"].toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        RuntimeControlType type;
        if (!runtimeControlTypeFromName(o["type"].toString(), type))
            continue;
        controls.append(controlFromJson(o, type));
    }
    return true;
}

// ==================== RuntimeInterface（多页） ====================

void RuntimeInterface::ensurePage()
{
    if (pages.isEmpty())
        pages.append(RuntimeInterfacePage());
    if (currentPageIndex < 0 || currentPageIndex >= pages.size())
        currentPageIndex = 0;
}

RuntimeInterfacePage *RuntimeInterface::page(int index)
{
    ensurePage();
    if (index < 0 || index >= pages.size()) return nullptr;
    return &pages[index];
}

RuntimeInterfacePage *RuntimeInterface::currentPage()
{
    ensurePage();
    return &pages[currentPageIndex];
}

int RuntimeInterface::addPage()
{
    RuntimeInterfacePage p;
    p.pageName = QStringLiteral("页面%1").arg(pages.size() + 1);
    pages.append(p);
    return pages.size() - 1;
}

void RuntimeInterface::removePage(int index)
{
    if (pages.size() <= 1) return;   // 至少保留一页
    if (index < 0 || index >= pages.size()) return;
    pages.removeAt(index);
    if (currentPageIndex >= pages.size())
        currentPageIndex = pages.size() - 1;
}

int RuntimeInterface::totalControlCount() const
{
    int n = 0;
    for (const RuntimeInterfacePage &p : pages)
        n += p.controls.size();
    return n;
}

QJsonObject RuntimeInterface::toJson() const
{
    QJsonObject root;
    QJsonArray pageArr;
    for (const RuntimeInterfacePage &p : pages)
        pageArr.append(p.toJson());
    root["pages"] = pageArr;
    root["currentPage"] = currentPageIndex;
    return root;
}

bool RuntimeInterface::fromJson(const QJsonObject &json)
{
    pages.clear();
    currentPageIndex = 0;

    // 项目未上线，只保留当前多页格式，不做旧单页格式兼容（2026-11-09 用户口径）
    if (!json.contains("pages")) return false;
    const QJsonArray pageArr = json["pages"].toArray();
    for (const QJsonValue &v : pageArr) {
        RuntimeInterfacePage p;
        if (p.fromJson(v.toObject()))
            pages.append(p);
    }
    currentPageIndex = json["currentPage"].toInt(0);
    ensurePage();
    return !pages.isEmpty();
}

bool RuntimeInterface::saveToFile(const QString &fileName) const
{
    // 原子写（QSaveFile）：运行界面布局是全局单文件，写坏即"界面全空"
    QSaveFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool RuntimeInterface::loadFromFile(const QString &fileName)
{
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QByteArray data = f.readAll();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    return fromJson(doc.object());
}

QStringList RuntimeInterface::staleBindings(const QSet<QString> &globals,
                                            const QSet<QString> &nodes) const
{
    QStringList bad;
    for (const RuntimeInterfacePage &page : pages) {
        for (const RuntimeControl &c : page.controls) {
            if (c.bindKey.isEmpty() || c.bindType.isEmpty())
                continue;
            if (c.bindType == QLatin1String("global") && !globals.contains(c.bindKey))
                bad << QStringLiteral("global:%1").arg(c.bindKey);
            // 节点集为空 = "未知"而非"失效"（2026-10-27 P3-6）：启动期先打开运行界面时
            // 项目还没载入，全部 node 绑定会被误判成失效（假红）。
            else if (c.bindType == QLatin1String("node") && !nodes.isEmpty()
                     && !nodes.contains(c.bindKey))
                bad << QStringLiteral("node:%1").arg(c.bindKey);
        }
    }
    bad.removeDuplicates();
    return bad;
}
