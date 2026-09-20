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
    return false;
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
    o["visible"] = c.visible;
    QJsonObject geo;
    geo["x"] = c.geometry.x();
    geo["y"] = c.geometry.y();
    geo["w"] = c.geometry.width();
    geo["h"] = c.geometry.height();
    o["geometry"] = geo;
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
    ctrl.visible = o["visible"].toBool(true);

    const QJsonObject geo = o["geometry"].toObject();
    ctrl.geometry = QRect(geo["x"].toInt(20), geo["y"].toInt(20),
                          geo["w"].toInt(240), geo["h"].toInt(160));

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

    if (json.contains("pages")) {
        // 新多页格式
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

    // 旧单页格式：整体包成一页（零迁移）
    if (!json.contains("controls")) return false;
    RuntimeInterfacePage p;
    if (!p.fromJson(json)) return false;
    if (p.pageName == QStringLiteral("页面1"))
        p.pageName = json["pageName"].toString(QStringLiteral("运行界面"));
    pages.append(p);
    return true;
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
