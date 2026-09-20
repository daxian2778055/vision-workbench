#include "GlobalVariableManager.h"
#include <QMutexLocker>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <QDebug>
#include "AppLog.h"

GlobalVariableManager::GlobalVariableManager(QObject *parent)
    : QObject(parent)
{}

GlobalVariableManager::~GlobalVariableManager()
{}

GlobalVariableManager *GlobalVariableManager::instance()
{
    // C++11 静态局部变量：线程安全的懒汉单例
    static GlobalVariableManager inst;
    return &inst;
}

bool GlobalVariableManager::addVariable(const QString &name, VariableType type, const QVariant &value, const QString &description)
{
    QMutexLocker lock(&m_mutex);
    if (m_variables.contains(name)) {
        VFP_DEBUG << "Variable" << name << "already exists";
        return false;
    }

    Variable var;
    var.name = name;
    var.type = type;
    var.value = value;
    var.description = description;

    m_variables[name] = var;
    VFP_DEBUG << "Added variable" << name << "with type" << type << "and value" << value;
    return true;
}

bool GlobalVariableManager::removeVariable(const QString &name)
{
    QMutexLocker lock(&m_mutex);
    if (!m_variables.contains(name)) {
        VFP_DEBUG << "Variable" << name << "does not exist";
        return false;
    }

    m_variables.remove(name);
    VFP_DEBUG << "Removed variable" << name;
    return true;
}

QVariant GlobalVariableManager::getVariable(const QString &name) const
{
    QMutexLocker lock(&m_mutex);
    if (!m_variables.contains(name)) {
        VFP_DEBUG << "Variable" << name << "does not exist";
        return QVariant();
    }

    return m_variables[name].value;
}

bool GlobalVariableManager::setVariable(const QString &name, const QVariant &value)
{
    QVariant newValue = value;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_variables.contains(name)) {
            VFP_DEBUG << "Variable" << name << "does not exist";
            return false;
        }

        Variable &var = m_variables[name];
        var.value = newValue;
    }
    VFP_DEBUG << "Updated variable" << name << "to" << newValue;

    // emit 放在锁外：信号槽可能是同步调用，重入 getVariable 会死锁
    emit variableChanged(name, newValue);
    return true;
}

QMap<QString, GlobalVariableManager::Variable> GlobalVariableManager::variables() const
{
    QMutexLocker lock(&m_mutex);
    return m_variables;
}

bool GlobalVariableManager::variableExists(const QString &name) const
{
    QMutexLocker lock(&m_mutex);
    return m_variables.contains(name);
}

GlobalVariableManager::VariableType GlobalVariableManager::getVariableType(const QString &name) const
{
    QMutexLocker lock(&m_mutex);
    if (!m_variables.contains(name)) {
        VFP_DEBUG << "Variable" << name << "does not exist";
        return IntType;
    }

    return m_variables[name].type;
}

QJsonObject GlobalVariableManager::toJson() const
{
    QMutexLocker lock(&m_mutex);
    QJsonObject root;
    QJsonArray variablesArray;
    for (auto it = m_variables.constBegin(); it != m_variables.constEnd(); ++it) {
        const Variable &var = it.value();
        QJsonObject varObj;
        varObj["name"] = var.name;
        varObj["type"] = var.type;
        varObj["value"] = QJsonValue::fromVariant(var.value);
        varObj["description"] = var.description;
        variablesArray.append(varObj);
    }
    root["variables"] = variablesArray;
    return root;
}

void GlobalVariableManager::fromJson(const QJsonObject &json)
{
    {
        QMutexLocker lock(&m_mutex);
        QJsonArray variablesArray = json["variables"].toArray();
        m_variables.clear();
        for (const QJsonValue &value : variablesArray) {
            if (value.isObject()) {
                QJsonObject varObj = value.toObject();
                Variable var;
                var.name = varObj["name"].toString();
                var.type = static_cast<VariableType>(varObj["type"].toInt());
                var.value = varObj["value"].toVariant();
                var.description = varObj["description"].toString();
                m_variables[var.name] = var;
            }
        }
    }
    emit variableChanged(QString(), QVariant());
}

bool GlobalVariableManager::saveToFile(const QString &fileName) const
{
    QJsonDocument doc(toJson());
    // 原子写（QSaveFile）：全局变量表是跨方案共享的单文件，写坏即全部变量丢失
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        VFP_DEBUG << "Failed to open file" << fileName << "for writing";
        return false;
    }

    const QByteArray payload = doc.toJson();
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        VFP_DEBUG << "Failed to write global variables (incomplete)" << fileName;
        return false;
    }
    if (!file.commit()) {
        VFP_DEBUG << "Failed to commit global variables" << fileName;
        return false;
    }
    VFP_DEBUG << "Saved global variables to" << fileName;
    return true;
}

bool GlobalVariableManager::loadFromFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        VFP_DEBUG << "Failed to open file" << fileName << "for reading";
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        VFP_DEBUG << "Invalid JSON format in file" << fileName;
        return false;
    }

    fromJson(doc.object());

    VFP_DEBUG << "Loaded" << m_variables.size() << "global variables from" << fileName;
    return true;
}