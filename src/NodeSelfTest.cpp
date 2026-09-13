#include "NodeSelfTest.h"
#include "NodeRegistry.h"
#include "NodeBase.h"
#include "AppLog.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <cstdio>

int runNodeSelfTest()
{
    // 确保注册表已填充
    registerAllNodes();

    const auto &regs = NodeRegistry::instance().all();
    QStringList report;
    int pass = 0, fail = 0, skip = 0;
    QStringList failList;

    report << QStringLiteral("VisionFlowPlatform 节点自检 %1")
                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    report << QStringLiteral("注册节点总数: %1").arg(regs.size());
    report << QString();

    for (const auto &reg : regs) {
        const QString id = reg.id;
        // 硬件/通讯源节点跳过（需要相机、串口等真实设备）
        if (id.contains(QStringLiteral("MvsImageSource"))
            || id.contains(QStringLiteral("HalconImageSource"))
            || id.contains(QStringLiteral("ReceiveData"))
            || id.contains(QStringLiteral("SendData"))) {
            report << QStringLiteral("SKIP  %1（需硬件/通讯设备）").arg(id);
            ++skip;
            continue;
        }
        NodeBase *node = NodeRegistry::instance().createById(id, nullptr);
        if (!node) {
            report << QStringLiteral("FAIL  %1（创建失败）").arg(id);
            failList << id;
            ++fail;
            continue;
        }
        QString status;
        try {
            node->init();
            if (node->inputPorts().isEmpty() && node->outputPorts().isEmpty()) {
                report << QStringLiteral("FAIL  %1（无输入/输出端口）").arg(id);
                failList << id;
                ++fail;
                delete node;
                continue;
            }
            QElapsedTimer t;
            t.start();
            node->run();
            const qint64 ms = t.elapsed();
            status = QStringLiteral("OK    %1（%2 端口，%3 ms）")
                         .arg(id)
                         .arg(node->inputPorts().size() + node->outputPorts().size())
                         .arg(ms);
            ++pass;
        } catch (const std::exception &e) {
            status = QStringLiteral("FAIL  %1（异常: %2）").arg(id, QString::fromLocal8Bit(e.what()));
            failList << id;
            ++fail;
        } catch (...) {
            status = QStringLiteral("FAIL  %1（未知异常）").arg(id);
            failList << id;
            ++fail;
        }
        report << status;
        delete node;
    }

    report << QString();
    report << QStringLiteral("==== 汇总: %1 PASS / %2 FAIL / %3 SKIP ====").arg(pass).arg(fail).arg(skip);
    if (!failList.isEmpty())
        report << QStringLiteral("失败节点: %1").arg(failList.join(QStringLiteral(", ")));

    const QString path = QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("selftest_nodes_report.txt"));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        for (const QString &line : report)
            out << line << '\n';
        f.close();
    }
    // 同时在 stdout 输出（调试终端可见）
    for (const QString &line : report)
        fprintf(stdout, "%s\n", qPrintable(line));
    fflush(stdout);

    return fail > 0 ? 1 : 0;
}
