#include "MainWindow.h"
#include "AppDatabase.h"
#include "DataObject.h"
#include "AppLog.h"
#include "NodeSelfTest.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSettings>
#include <halconcpp/HalconCpp.h>
#include "HalconEnvCheck.h"
#include <exception>
#include <csignal>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <windows.h>
#include <dbghelp.h>

using namespace HalconCpp;

// 注册Halcon类型到Qt元对象系统
Q_DECLARE_METATYPE(HalconCpp::HImage)
Q_DECLARE_METATYPE(HalconCpp::HRegion)
Q_DECLARE_METATYPE(HalconCpp::HXLD)

// ---------- 异常信息收集与崩溃日志 ----------
namespace {

QMutex g_crashMutex;

// ---- 数据库历史数据自动清理策略 ----
constexpr int kDatabaseRetainDays = 30;                        ///< 报警/检测结果/操作日志保留天数
constexpr int kDatabasePurgeFirstDelayMs = 60 * 1000;          ///< 启动后首次清理的延迟
constexpr int kDatabasePurgeIntervalMs = 24 * 60 * 60 * 1000;  ///< 之后每日清理一次

/// 将所有 Qt 警告/错误/致命日志同时写入崩溃日志文件
void crashLogHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(ctx)
    static const QString levelStr[] = {
        QStringLiteral("Debug"), QStringLiteral("Warning"),
        QStringLiteral("Critical"), QStringLiteral("Fatal"), QStringLiteral("Info")
    };
    int idx = qBound(0, static_cast<int>(type), 4);

    QMutexLocker locker(&g_crashMutex);
    QString dir = QDir::current().absoluteFilePath(QStringLiteral("logs"));
    QDir().mkpath(dir);
    QFile file(dir + QStringLiteral("/crash.log"));
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            << " [" << levelStr[idx] << "] " << msg << '\n';
        file.close();
    }

    // 保留默认输出
    if (type != QtDebugMsg)
        fprintf(stderr, "%s\n", qPrintable(msg));
}

/// 未捕获 C++ 异常记录
void uncaughtExceptionHandler()
{
    try {
        throw;
    } catch (const std::exception &e) {
        QMutexLocker locker(&g_crashMutex);
        QString dir = QDir::current().absoluteFilePath(QStringLiteral("logs"));
        QDir().mkpath(dir);
        QFile file(dir + QStringLiteral("/crash.log"));
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                << " [UncaughtException] " << e.what() << '\n';
            file.close();
        }
        fprintf(stderr, "Uncaught exception: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "Uncaught unknown exception\n");
    }
    std::terminate();
}

/// 崩溃调用栈（利用启动时初始化的符号表解析函数名）
void dumpStackToLog(QTextStream &out)
{
    constexpr int kMaxFrames = 32;
    void *frames[kMaxFrames] = { nullptr };
    const USHORT count = CaptureStackBackTrace(0, kMaxFrames, frames, nullptr);

    out << "  Stack trace (" << count << " frames):\n";
    // 跳过本函数与 signalHandler 自身
    const int skip = 2;
    for (int i = skip; i < count; ++i) {
        char symbolBuf[sizeof(SYMBOL_INFO) + 256] = { 0 };
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(symbolBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        if (SymFromAddr(GetCurrentProcess(), addr, nullptr, sym)) {
            out << QStringLiteral("    #%1  0x%2  %3\n")
                       .arg(i - skip, 2)
                       .arg(addr, 0, 16)
                       .arg(QString::fromLocal8Bit(sym->Name, static_cast<int>(sym->NameLen)));
        } else {
            out << QStringLiteral("    #%1  0x%2\n")
                       .arg(i - skip, 2)
                       .arg(addr, 0, 16);
        }
    }
}

void signalHandler(int sig)
{
    QMutexLocker locker(&g_crashMutex);
    QString dir = QDir::current().absoluteFilePath(QStringLiteral("logs"));
    QDir().mkpath(dir);
    QFile file(dir + QStringLiteral("/crash.log"));
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            << " [CrashSignal] " << sig << '\n';
        dumpStackToLog(out);
        file.close();
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

/// SEH 异常（访问违例等）处理：记录文本栈 + 生成 minidump（.dmp），供现场回溯
LONG WINAPI sehHandler(EXCEPTION_POINTERS *ep)
{
    QString dir = QDir::current().absoluteFilePath(QStringLiteral("logs"));
    QDir().mkpath(dir);

    // 1) 文本栈
    {
        QMutexLocker locker(&g_crashMutex);
        QFile file(dir + QStringLiteral("/crash.log"));
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                << " [SEH Exception] code=0x"
                << QString::number(static_cast<quint32>(ep ? ep->ExceptionRecord->ExceptionCode : 0), 16)
                << '\n';
            dumpStackToLog(out);
            file.close();
        }
    }

    // 2) minidump（供 WinDbg/VS 离线分析）
    {
        QMutexLocker locker(&g_crashMutex);
        const QString dmpPath = dir + QStringLiteral("/crash.dmp");
        HANDLE hFile = CreateFileW(dmpPath.toStdWString().c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            memset(&mei, 0, sizeof(mei));
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                              MiniDumpNormal, ep ? &mei : nullptr, nullptr, nullptr);
            CloseHandle(hFile);
        }
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

int main(int argc, char *argv[])
{
    // 单实例保护：多开会导致相机/串口/SQLite 资源冲突
    const QString lockPath =
        QDir::temp().filePath(QStringLiteral("VisionFlowPlatform.lock"));
    QLockFile instanceLock(lockPath);
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(100)) {
        // 已有一个实例在运行：提示后退出
        QMessageBox::warning(nullptr, QStringLiteral("VisionFlowPlatform"),
                             QStringLiteral("程序已在运行，请勿重复启动。"));
        return 1;
    }

    try {
        qInstallMessageHandler(crashLogHandler);
        std::set_terminate(uncaughtExceptionHandler);
        std::signal(SIGABRT, signalHandler);
        std::signal(SIGSEGV, signalHandler);
        std::signal(SIGILL, signalHandler);
        std::signal(SIGFPE, signalHandler);

        // 未处理 SEH 异常（访问违例等）统一走 minidump + 文本栈
        SetUnhandledExceptionFilter(sehHandler);

        // 初始化符号表，供崩溃调用栈解析函数名
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);

        QApplication a(argc, argv);

        // 节点自检模式：遍历全部注册节点实例化+init+空输入运行，输出报告后退出
        if (a.arguments().contains(QStringLiteral("--selftest-nodes"))) {
            return runNodeSelfTest();
        }

        // Initialize application database
        AppDatabase::instance()->initialize();

        // 启动不做 HALCON 区域/测量自检。帮助菜单可检测图像层；
        // 仅当方案含 DeepOCR / Halcon图像源时提示需要 HALCON 运行时。

        // 历史数据清理（控制数据库增长）。
        // 注意：purgeOldRecords 内含 VACUUM（重写整个库文件），原先在启动路径上同步执行，
        // 直接挤占启动时间并与 NFR1.2（启动 ≤3 秒）冲突；而连续运行数周的设备也只在重启时才清理。
        // 改为：启动后延迟执行 → 之后每日一次 → 放到后台线程执行（不阻塞界面）。
        // 后台线程可安全访问数据库：AppDatabase 已按线程建立独立连接（见 threadDatabase）。
        auto *purgeTimer = new QTimer(&a);
        purgeTimer->setInterval(kDatabasePurgeIntervalMs);
        QObject::connect(purgeTimer, &QTimer::timeout, &a, []() {
            QtConcurrent::run([]() {
                AppDatabase::instance()->purgeOldRecords(kDatabaseRetainDays);
            });
        });
        QTimer::singleShot(kDatabasePurgeFirstDelayMs, &a, [purgeTimer]() {
            QtConcurrent::run([]() {
                AppDatabase::instance()->purgeOldRecords(kDatabaseRetainDays);
            });
            purgeTimer->start();   // 首次完成后开始每日周期
        });

        qRegisterMetaType<HalconCpp::HImage>("HImage");
        qRegisterMetaType<HalconCpp::HRegion>("HRegion");
        qRegisterMetaType<HalconCpp::HXLD>("HXLD");
        qRegisterMetaType<MeasureResult>("MeasureResult");
        qRegisterMetaType<QVector<double>>("QVector<double>");

        MainWindow w;
        w.show();

        int result = a.exec();
#ifndef QT_NO_DEBUG
        VFP_DEBUG << "Event loop exited with result:" << result;
#endif
        return result;
    } catch (const std::exception &e) {
        qWarning() << "Exception in main:" << e.what();
        return 1;
    } catch (...) {
        qWarning() << "Unknown exception in main";
        return 1;
    }
}
