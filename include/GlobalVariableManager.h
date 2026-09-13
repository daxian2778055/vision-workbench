#ifndef GLOBALVARIABLEMANAGER_H
#define GLOBALVARIABLEMANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QVariant>
#include <QMutex>

class GlobalVariableManager : public QObject
{
    Q_OBJECT

public:
    enum VariableType {
        IntType,
        BoolType,
        FloatType,
        StringType
    };

    struct Variable {
        QString name;
        VariableType type;
        QVariant value;
        QString description;
    };

    static GlobalVariableManager *instance();

    // 添加全局变量
    bool addVariable(const QString &name, VariableType type, const QVariant &value, const QString &description = "");

    // 删除全局变量
    bool removeVariable(const QString &name);

    // 获取全局变量
    QVariant getVariable(const QString &name) const;

    // 设置全局变量
    bool setVariable(const QString &name, const QVariant &value);

    // 获取所有全局变量
    QMap<QString, Variable> variables() const;

    // 检查变量是否存在
    bool variableExists(const QString &name) const;

    // 获取变量类型
    VariableType getVariableType(const QString &name) const;

    // 保存全局变量到文件
    bool saveToFile(const QString &fileName) const;

    // 从文件加载全局变量
    bool loadFromFile(const QString &fileName);

    // 序列化到 JSON（供项目文件整体保存）
    QJsonObject toJson() const;
    // 从 JSON 恢复（供项目文件整体加载）
    void fromJson(const QJsonObject &json);

signals:
    // 当全局变量发生变化时发出
    void variableChanged(const QString &name, const QVariant &value);

private:
    GlobalVariableManager(QObject *parent = nullptr);
    ~GlobalVariableManager();

    /// m_variables 可由运行时线程写（计数器等）、界面线程读，QMap 非线程安全，须加锁
    mutable QMutex m_mutex;
    QMap<QString, Variable> m_variables;
};

#endif // GLOBALVARIABLEMANAGER_H