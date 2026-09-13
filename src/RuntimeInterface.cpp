#include "RuntimeInterface.h"
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
    return false;
}

RuntimeControl *RuntimeInterface::addControl(RuntimeControlType type, const QRect &geo)
{
    RuntimeControl ctrl;
    ctrl.type = type;
    ctrl.title = ctrl.defaultTitle();
    ctrl.geometry = geo;
    controls.append(ctrl);
    return &controls.last();
}

void RuntimeInterface::removeControl(int index)
{
    if (index >= 0 && index < controls.size())
        controls.removeAt(index);
}

void RuntimeInterface::clear()
{
    controls.clear();
}

int RuntimeInterface::indexOf(const RuntimeControl *ctrl) const
{
    for (int i = 0; i < controls.size(); ++i) {
        if (&controls[i] == ctrl) return i;
    }
    return -1;
}

QJsonObject RuntimeInterface::toJson() const
{
    QJsonObject root;
    root["pageName"] = pageName;

    QJsonArray arr;
    for (const RuntimeControl &c : controls) {
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
        arr.append(o);
    }
    root["controls"] = arr;
    return root;
}

bool RuntimeInterface::fromJson(const QJsonObject &json)
{
    if (!json.contains("controls")) return false;

    controls.clear();
    pageName = json["pageName"].toString(QStringLiteral("运行界面"));

    const QJsonArray arr = json["controls"].toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        RuntimeControlType type;
        if (!runtimeControlTypeFromName(o["type"].toString(), type))
            continue;
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
        controls.append(ctrl);
    }
    return true;
}

bool RuntimeInterface::saveToFile(const QString &fileName) const
{
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
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
