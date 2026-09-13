#pragma once

#include "HalconNode.h"
#include <QProcess>
#include <QStringList>

/// Script execution node that runs Python/Lua scripts inline
class ScriptNode : public HalconNode
{
    Q_OBJECT

public:
    explicit ScriptNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch) override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    QString executePythonScript(const QString &script, const QStringList &args);
    QString executeLuaScript(const QString &script, const QStringList &args);
};
