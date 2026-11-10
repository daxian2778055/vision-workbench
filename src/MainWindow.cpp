#include <QElapsedTimer>
#include "MainWindow.h"
#include "FocusGuard.h"
#include "VmSharedText.h"
#include "CalibrationWizardDialog.h"
#include "ResultTablePanel.h"
#include "VariablePanel.h"
#include "AuxPanelManager.h"
#include "ImageDisplayController.h"
#include "ExecutionStatusController.h"
#include "RecentFilesMenu.h"
#include "ui_MainWindow.h"
#include "FlowScene.h"
#include "FlowCanvasView.h"
#include "FlowExecutorToken.h"   // 词条切片 C P3b 期：执行器本体的唯一拥有者（令牌）
#include "NodeBase.h"
#include "NodeGroupItem.h"
#include "FlowSnippet.h"
#include "ProjectPaths.h"   // U-201 载入门禁：目录外路径的确认清单（U-215 含相对逃逸）
#include "YieldMonitor.h"
#include "InspectionRecord.h"
#include "ReportAutoExport.h"
#include <QClipboard>
#include <algorithm>
#include "HalconEnvCheck.h"
#include "HalconNode.h"
#include "HelpDialog.h"
#include "ManualPathResolver.h"   // 切片 B 手册解析族首抽：手册候选链与首读者胜出（判据见头件）
#include "RoundVariableFeed.h"    // 切片 B 变量族首抽：变量域两出口装配（判据见头件）
#include "NodeIconPalette.h"      // 切片 B 图标族首抽：类别色语义表与圆标合成（判据见头件）
#include "CalibrationBoard.h"     // P2 工具菜单：标定板图案渲染与保存（纯数据面）
#include "DiagnosticsCollector.h" // P2 工具菜单：异常信息收集报告生成与导出（纯数据面）
#include "UpstreamWalk.h"         // 切片 B 图谱族首抽：输入端口上游闭包游走（判据见头件）
#include "FindLineNode.h"
#include "FindCircleNode.h"
#include "CaliperMeasureNode.h"
#include "MvsImageSourceNode.h"
#include "HalconImageSourceNode.h"
#include "DeepOcrNode.h"
#include "ColorConversionNode.h"
#include "ImageReadNode.h"
#include "DataObject.h"
#include "FlowExecutor.h"
#include "ExecutionControlPolicy.h"   // 切片 B 首抽：四控制槽的判定核（判据见头件）
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QProcess>
#include "Version.h"   // CMake 注入：版本号 / 提交短哈希 / 配置日期（#520）
#include "Port.h"
#include "Connection.h"
#include "NodeGraphicsItem.h"
#include "ProjectManager.h"
#include "ProjectLoadNotes.h"   // U-34：载入留痕接可见面
#include "AppDatabase.h"
#include "BrandHeader.h"
#include "GlobalVariableManager.h"
#include "GlobalVariableDialog.h"
#include "FlowVariableDialog.h"
#include "UserLoginDialog.h"
#include "UserManagementDialog.h"
#include "RecipeManager.h"
#include "RecipeDialog.h"
#include "AlarmHistoryDialog.h"
#include "InspectionResultDialog.h"
#include "LargeFileStoreDialog.h"
#include "LargeFileImageStore.h"
#include "CommunicationManager.h"
#include "CommunicationManagerDialog.h"
#include "CommMonitorDialog.h"
#include "CommResponseManager.h"
#include "GlobalTriggerManager.h"
#include "GlobalTriggerDialog.h"
#include "SessionManager.h"
#include "ScriptSecurityPolicy.h"
#include "ComputeBackend.h"
#include "FeatureLibraryDialog.h"
#include "ProjectPackager.h"
#include "TrainingWizardDialog.h"
#include "BoxAnnotatorDialog.h"
#include "TrainingRunnerDialog.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QTabBar>
#include <QSignalBlocker>
#include <QActionGroup>   // 系统菜单「脚本安全：执行前确认」开关
#include "FactoryPasswordGuard.h"
#include "OperationLogDialog.h"
#include "ParameterSearchDialog.h"
#include "CodeExportDialog.h"
#include "ReportDialog.h"
#include "RuntimeInterfaceDesigner.h"
#include "RuntimeInterfaceView.h"
#include <QShortcut>
#include "HeartbeatManager.h"
#include "GlobalCameraManager.h"
#include "GlobalCameraDialog.h"
#include "HalconWindow.h"
#include "PerformancePanel.h"
#include "StatisticsReportDialog.h"
#include "OutputDataViewer.h"
#include "NodeSearchWidget.h"
#include "HelpViewer.h"
#include "ModuleEditorDialog.h"
#include <HalconCpp.h>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QThreadPool>
#include <QTimer>
#include <QGraphicsView>
#include <QPainter>
#include <QPixmap>      // 模块箱分类图标（本地绘制，不依赖外部图标资源）
#include <QPolygonF>
#include <QDateTime>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QGraphicsView>
#include <QToolBar>
#include <QScrollBar>
#include <QStorageInfo>
#include <QFileInfo>
#include <QTableWidget>
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>
#include <QSysInfo>
#include <QTabWidget>
#include <QGroupBox>
#include <QGridLayout>
#include <QPointer>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QEventLoop>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QColorDialog>
#include "ImageReadNode.h"
#include "Connection.h"

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "VisionWorkbenchStyle.h"
#include "NodeFactory.h"
#include "NodeRegistry.h"
#include "WelcomeDialog.h"

using namespace HalconCpp;
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include <QTabBar>
#include <QShortcut>
#include <QSettings>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QPointer>
#include <cmath>
#include <QPushButton>
#include <QComboBox>
#include <QDockWidget>
#include <QSizePolicy>
#include "AppLog.h"
#include "UiLogBus.h"

namespace {

/// 流程模式 → 标签页后缀（F-3：此前组合框/载入/切页签各写一份，且越界行为不一致，统一到此处）
QString flowModeSuffix(FlowMode mode)
{
    switch (mode) {
    case FlowMode::Continuous:
        return QStringLiteral(" [连续]");
    case FlowMode::HardwareTrigger:
        return QStringLiteral(" [硬触发]");
    case FlowMode::SoftwareTrigger:
    default:
        return QStringLiteral(" [软触发]");
    }
}

// 枚举顺序 = 组合框索引 = scene->flowMode 的持久化取值，三者必须同步，改动即在此报错
static_assert(static_cast<int>(FlowMode::Continuous) == 0
                  && static_cast<int>(FlowMode::SoftwareTrigger) == 1
                  && static_cast<int>(FlowMode::HardwareTrigger) == 2,
              "FlowMode 顺序变更必须同步：组合框顺序 / scene->flowMode 持久化取值 / flowModeSuffix");

/// 模块箱分类图标（实装 4.4）：24px 透明底线框字形，按分类语义区分。
/// 只用 QPainter 本地绘制，不依赖外部图标资源（仓库内没有 VM 图标资产）。
QIcon toolCategoryGlyph(const QString &group)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // VM 左侧模块箱资产实测：字形 24–28px、亮部 ≈#afb4b4、笔画约 2px
    const QColor ink(0xb0, 0xb4, 0xb4);
    painter.setPen(QPen(ink, 1.8));
    painter.setBrush(Qt::NoBrush);

    if (group == QStringLiteral("图像采集")) {
        // 相机：机身 + 镜头 + 顶部取景凸起
        painter.drawRoundedRect(QRectF(3, 8, 18, 12), 2, 2);
        painter.drawEllipse(QPointF(12, 14), 3.2, 3.2);
        painter.drawRect(QRectF(8, 5, 8, 3));
    } else if (group == QStringLiteral("图像处理")) {
        // 图像：画框 + 太阳 + 山形折线
        painter.drawRect(QRectF(3.5, 4.5, 17, 15));
        painter.drawEllipse(QPointF(8.5, 9.5), 1.7, 1.7);
        painter.drawPolyline(QPolygonF() << QPointF(5, 17) << QPointF(10, 11.5)
                                          << QPointF(14, 15) << QPointF(18.5, 10.5));
    } else if (group == QStringLiteral("特征定位")) {
        // 十字准星：圆环 + 四向刻度 + 中心点
        painter.drawEllipse(QPointF(12, 12), 5.5, 5.5);
        painter.drawLine(QPointF(12, 3), QPointF(12, 6.5));
        painter.drawLine(QPointF(12, 17.5), QPointF(12, 21));
        painter.drawLine(QPointF(3, 12), QPointF(6.5, 12));
        painter.drawLine(QPointF(17.5, 12), QPointF(21, 12));
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(12, 12), 1.2, 1.2);
        painter.setBrush(Qt::NoBrush);
    } else if (group == QStringLiteral("几何测量")) {
        // 卡尺：横梁 + 两卡爪 + 刻度
        painter.drawLine(QPointF(4, 6.5), QPointF(20, 6.5));
        painter.drawLine(QPointF(6, 6.5), QPointF(6, 18));
        painter.drawLine(QPointF(17, 6.5), QPointF(17, 18));
        painter.drawLine(QPointF(6, 18), QPointF(17, 18));
        painter.drawLine(QPointF(9, 6.5), QPointF(9, 10));
        painter.drawLine(QPointF(12, 6.5), QPointF(12, 9));
        painter.drawLine(QPointF(15, 6.5), QPointF(15, 10));
    } else if (group == QStringLiteral("深度学习")) {
        // 神经网络：三层节点 + 连线
        const QPointF left[3] = {QPointF(4.5, 6), QPointF(4.5, 12), QPointF(4.5, 18)};
        const QPointF mid[2] = {QPointF(12, 9), QPointF(12, 15)};
        painter.drawEllipse(QPointF(19.5, 12), 1.8, 1.8);
        for (int i = 0; i < 3; ++i) {
            painter.drawEllipse(left[i], 1.8, 1.8);
            painter.drawLine(left[i], mid[i / 2]);
        }
        for (int i = 0; i < 2; ++i) {
            painter.drawEllipse(mid[i], 1.8, 1.8);
            painter.drawLine(mid[i], QPointF(19.5, 12));
        }
    } else if (group == QStringLiteral("相机 IO")) {
        // 芯片：方框 + 四向引脚
        painter.drawRect(QRectF(6, 6, 12, 12));
        for (int i = 0; i < 3; ++i) {
            const qreal offset = 9 + i * 3;
            painter.drawLine(QPointF(offset, 2.5), QPointF(offset, 6));
            painter.drawLine(QPointF(offset, 18), QPointF(offset, 21.5));
            painter.drawLine(QPointF(2.5, offset), QPointF(6, offset));
            painter.drawLine(QPointF(18, offset), QPointF(21.5, offset));
        }
    } else if (group == QStringLiteral("标定")) {
        // 标定板：棋盘格（两格填色）
        painter.drawRect(QRectF(4, 4, 16, 16));
        painter.drawLine(QPointF(4, 12), QPointF(20, 12));
        painter.drawLine(QPointF(12, 4), QPointF(12, 20));
        painter.setBrush(ink);
        painter.drawRect(QRectF(12, 4, 8, 8));
        painter.drawRect(QRectF(4, 12, 8, 8));
        painter.setBrush(Qt::NoBrush);
    } else if (group == QStringLiteral("识别")) {
        // 条码：粗细不一的竖线
        const qreal xs[] = {4, 6, 9, 12.5, 15.5, 18, 20.5};
        for (int i = 0; i < 7; ++i) {
            painter.setPen(QPen(ink, (i == 2 || i == 4) ? 2.2 : 1.2));
            painter.drawLine(QPointF(xs[i], 5), QPointF(xs[i], 19));
        }
        painter.setPen(QPen(ink, 1.8));
    } else if (group == QStringLiteral("跟踪")) {
        // 轨迹：折线 + 箭头 + 起点圆
        painter.drawPolyline(QPolygonF() << QPointF(4, 19) << QPointF(9, 13)
                                          << QPointF(14, 15) << QPointF(19, 7));
        painter.drawLine(QPointF(19, 7), QPointF(15.5, 7.5));
        painter.drawLine(QPointF(19, 7), QPointF(18.2, 10.5));
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(4, 19), 1.6, 1.6);
        painter.setBrush(Qt::NoBrush);
    } else if (group == QStringLiteral("逻辑与控制")) {
        // 判定菱形 + 分支出口
        painter.drawPolygon(QPolygonF() << QPointF(12, 3) << QPointF(21, 12)
                                         << QPointF(12, 21) << QPointF(3, 12));
        painter.drawLine(QPointF(21, 12), QPointF(23, 12));
    } else if (group == QStringLiteral("结果输出")) {
        // 输出：方框 + 出箭头
        painter.drawRect(QRectF(3.5, 4.5, 10, 15));
        painter.drawLine(QPointF(13.5, 12), QPointF(21, 12));
        painter.drawLine(QPointF(21, 12), QPointF(17.5, 8.5));
        painter.drawLine(QPointF(21, 12), QPointF(17.5, 15.5));
    } else {
        // 「全部模块」/未知分类：四宫格
        painter.drawRect(QRectF(4, 4, 7, 7));
        painter.drawRect(QRectF(13, 4, 7, 7));
        painter.drawRect(QRectF(4, 13, 7, 7));
        painter.drawRect(QRectF(13, 13, 7, 7));
    }
    return QIcon(pixmap);
}

/// 快捷工具条图标（实装 4.4）：与模块箱同风格的单色线框字形。
enum class ToolGlyph {
    New,
    Open,
    Save,
    Undo,
    Redo,
    Lock,
    Camera,
    Variables,
    Comm,
    Trigger,
    Script,
    Start,
    Stop,
    SingleShot,
    Step,
    Pause,
    Config,
    RuntimeUi,
    Search,
    Fit,
    ActualSize,
    ZoomIn,
    ZoomOut,
    Crosshair,
    Float,
    DualView,
    CrosshairConfig,
    FullScreen,
    Overlay,
    LoopRun,
    More
};

// 默认色/笔画取 VM 主工具条实测（亮部 ≈#c8c8c8、笔画 ≈1.5–2px）；
// 图像工具条按自己的 VM 资产实测传入 #9a9a9a/2.0。
QIcon toolbarGlyph(ToolGlyph glyph, const QColor &ink = QColor(0xc8, 0xc8, 0xc8),
                   qreal penWidth = 1.7)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(ink, penWidth));
    painter.setBrush(Qt::NoBrush);

    switch (glyph) {
    case ToolGlyph::New: {
        // 文档 + 右下角加号
        painter.drawRect(QRectF(5, 3, 11, 16));
        painter.drawLine(QPointF(8, 7), QPointF(13, 7));
        painter.drawLine(QPointF(8, 10), QPointF(13, 10));
        painter.drawLine(QPointF(17.5, 14), QPointF(17.5, 20));
        painter.drawLine(QPointF(14.5, 17), QPointF(20.5, 17));
        break;
    }
    case ToolGlyph::Open: {
        // 打开文件夹：底面 + 翻盖
        painter.drawPolyline(QPolygonF() << QPointF(3, 19) << QPointF(5, 9) << QPointF(10, 9)
                                          << QPointF(11.5, 11) << QPointF(21, 11));
        painter.drawLine(QPointF(3, 19), QPointF(19, 19));
        painter.drawLine(QPointF(19, 19), QPointF(21, 11));
        break;
    }
    case ToolGlyph::Save: {
        // 软盘：外框 + 顶部翻盖 + 底部标签
        painter.drawRect(QRectF(4, 4, 16, 16));
        painter.drawRect(QRectF(8, 4, 8, 6));
        painter.drawRect(QRectF(7, 13.5, 10, 6.5));
        break;
    }
    case ToolGlyph::Undo:
    case ToolGlyph::Redo: {
        const bool undo = (glyph == ToolGlyph::Undo);
        painter.save();
        if (!undo) {
            painter.translate(24, 0);
            painter.scale(-1, 1);
        }
        QPainterPath path;
        path.moveTo(20, 16);
        path.cubicTo(18, 8, 10, 6, 6, 10);
        painter.drawPath(path);
        // 箭头指向左（重做镜像后指向右）
        painter.drawLine(QPointF(6, 10), QPointF(10, 8));
        painter.drawLine(QPointF(6, 10), QPointF(9.5, 13));
        painter.restore();
        break;
    }
    case ToolGlyph::Lock: {
        // 锁体 + 锁梁
        painter.drawRoundedRect(QRectF(6, 11, 12, 9), 2, 2);
        QPainterPath shackle;
        shackle.moveTo(8, 11);
        shackle.lineTo(8, 8);
        shackle.arcTo(QRectF(8, 4, 8, 8), 180, -180);
        shackle.lineTo(16, 11);
        painter.drawPath(shackle);
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(12, 15), 1.4, 1.4);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::Camera: {
        // 相机：机身 + 镜头 + 取景凸起
        painter.drawRoundedRect(QRectF(3, 8, 18, 12), 2, 2);
        painter.drawEllipse(QPointF(12, 14), 3.2, 3.2);
        painter.drawRect(QRectF(8, 5, 8, 3));
        break;
    }
    case ToolGlyph::Variables: {
        // 变量：左右花括号 + 中间等号
        painter.drawPolyline(QPolygonF() << QPointF(9, 4) << QPointF(6, 4) << QPointF(6, 11)
                                          << QPointF(4.5, 12) << QPointF(6, 13) << QPointF(6, 20)
                                          << QPointF(9, 20));
        painter.drawPolyline(QPolygonF() << QPointF(15, 4) << QPointF(18, 4) << QPointF(18, 11)
                                          << QPointF(19.5, 12) << QPointF(18, 13) << QPointF(18, 20)
                                          << QPointF(15, 20));
        painter.drawLine(QPointF(10, 11), QPointF(14, 11));
        painter.drawLine(QPointF(10, 13), QPointF(14, 13));
        break;
    }
    case ToolGlyph::Comm: {
        // 通讯：地球（外圆 + 经线 + 赤道线）
        painter.drawEllipse(QPointF(12, 12), 8, 8);
        painter.drawEllipse(QPointF(12, 12), 3.8, 8);
        painter.drawLine(QPointF(4, 12), QPointF(20, 12));
        break;
    }
    case ToolGlyph::Trigger: {
        // 闪电（触发）
        painter.setBrush(ink);
        painter.drawPolygon(QPolygonF() << QPointF(13, 2) << QPointF(6, 13) << QPointF(11, 13)
                                         << QPointF(9, 22) << QPointF(18, 10) << QPointF(13, 10));
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::Script: {
        // 文档 + </>
        painter.drawRect(QRectF(4, 3, 16, 18));
        painter.drawPolyline(QPolygonF() << QPointF(10, 8) << QPointF(7, 12) << QPointF(10, 16));
        painter.drawPolyline(QPolygonF() << QPointF(14, 8) << QPointF(17, 12) << QPointF(14, 16));
        painter.drawLine(QPointF(13, 7), QPointF(11, 17));
        break;
    }
    case ToolGlyph::Start: {
        // 播放三角（实心）
        painter.setBrush(ink);
        painter.drawPolygon(QPolygonF() << QPointF(8, 5) << QPointF(19, 12) << QPointF(8, 19));
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::Stop: {
        // 停止方块（实心）
        painter.setBrush(ink);
        painter.drawRoundedRect(QRectF(6, 6, 12, 12), 2, 2);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::SingleShot: {
        // 单次执行：环形箭头 + 中心点
        QPainterPath arc;
        arc.arcMoveTo(QRectF(5, 5, 14, 14), 60);
        arc.arcTo(QRectF(5, 5, 14, 14), 60, 280);
        painter.drawPath(arc);
        painter.drawLine(QPointF(16.6, 5.6), QPointF(13.6, 5.2));
        painter.drawLine(QPointF(16.6, 5.6), QPointF(16.2, 9.0));
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(12, 12), 1.4, 1.4);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::LoopRun: {
        // 循环运行：环形箭头 + 中心播放三角（VM 流程栏第二枚运行控制同形）
        QPainterPath arc;
        arc.arcMoveTo(QRectF(5, 5, 14, 14), 60);
        arc.arcTo(QRectF(5, 5, 14, 14), 60, 280);
        painter.drawPath(arc);
        painter.drawLine(QPointF(16.6, 5.6), QPointF(13.6, 5.2));
        painter.drawLine(QPointF(16.6, 5.6), QPointF(16.2, 9.0));
        painter.setBrush(ink);
        painter.drawPolygon(QPolygonF() << QPointF(9.5, 8.5) << QPointF(15.5, 12.0)
                                        << QPointF(9.5, 15.5));
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::More: {
        // 更多：三点横排（收进次要视图操作的溢出菜单）
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(7, 12), 1.5, 1.5);
        painter.drawEllipse(QPointF(12, 12), 1.5, 1.5);
        painter.drawEllipse(QPointF(17, 12), 1.5, 1.5);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::Step: {
        // 单步：播放三角 + 竖向停止线（步进符号）
        painter.setBrush(ink);
        painter.drawPolygon(QPolygonF() << QPointF(6, 6) << QPointF(15, 12) << QPointF(6, 18));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(17.5, 5.5), QPointF(17.5, 18.5));
        break;
    }
    case ToolGlyph::Pause: {
        // 暂停：两条竖杠（实心）
        painter.setBrush(ink);
        painter.drawRoundedRect(QRectF(7, 5, 3.5, 14), 1, 1);
        painter.drawRoundedRect(QRectF(13.5, 5, 3.5, 14), 1, 1);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::Config: {
        // 综合配置：三行滑杆 + 旋钮
        painter.drawLine(QPointF(3, 7), QPointF(21, 7));
        painter.drawLine(QPointF(3, 12), QPointF(21, 12));
        painter.drawLine(QPointF(3, 17), QPointF(21, 17));
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(8, 7), 1.8, 1.8);
        painter.drawEllipse(QPointF(15, 12), 1.8, 1.8);
        painter.drawEllipse(QPointF(11, 17), 1.8, 1.8);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::RuntimeUi: {
        // 运行界面：窗口 + 出箭头
        painter.drawRect(QRectF(3, 5, 13, 14));
        painter.drawLine(QPointF(15, 12), QPointF(21, 12));
        painter.drawLine(QPointF(21, 12), QPointF(17.5, 8.5));
        painter.drawLine(QPointF(21, 12), QPointF(17.5, 15.5));
        break;
    }
    case ToolGlyph::Search: {
        // 参数查找：放大镜
        painter.drawEllipse(QPointF(10.5, 10), 5.5, 5.5);
        painter.drawLine(QPointF(14.5, 14.5), QPointF(21, 21));
        break;
    }
    case ToolGlyph::Fit: {
        // 适应窗口：VM 资产是「2x2 视窗格」，左上格带内容纹理
        painter.drawRect(QRectF(4, 4, 16, 16));
        painter.drawLine(QPointF(12, 4), QPointF(12, 20));
        painter.drawLine(QPointF(4, 12), QPointF(20, 12));
        painter.fillRect(QRectF(5, 5, 6.5, 6.5), QColor(ink.red(), ink.green(), ink.blue(), 70));
        break;
    }
    case ToolGlyph::ActualSize: {
        // 1:1 实际像素：圆角外框 + 文字（与 VM 资产的方框内「1:1」一致）
        painter.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        QFont font = painter.font();
        font.setPixelSize(11);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRectF(3, 4, 18, 16), Qt::AlignCenter, QStringLiteral("1:1"));
        break;
    }
    case ToolGlyph::ZoomIn:
    case ToolGlyph::ZoomOut: {
        // 放大/缩小：放大镜 + 加/减
        painter.drawEllipse(QPointF(10, 10), 6, 6);
        painter.drawLine(QPointF(14.5, 14.5), QPointF(21, 21));
        painter.drawLine(QPointF(7, 10), QPointF(13, 10));
        if (glyph == ToolGlyph::ZoomIn)
            painter.drawLine(QPointF(10, 7), QPointF(10, 13));
        break;
    }
    case ToolGlyph::Crosshair: {
        // 十字辅助线
        painter.drawLine(QPointF(12, 4), QPointF(12, 20));
        painter.drawLine(QPointF(4, 12), QPointF(20, 12));
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(12, 12), 1.4, 1.4);
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case ToolGlyph::Float: {
        // 弹出窗口：窗口 + 右上出箭头
        painter.drawRect(QRectF(4, 8, 12, 12));
        painter.drawLine(QPointF(13, 4), QPointF(21, 4));
        painter.drawLine(QPointF(21, 4), QPointF(21, 12));
        painter.drawLine(QPointF(21, 4), QPointF(13, 12));
        break;
    }
    case ToolGlyph::DualView: {
        // 双画面：两个并排视窗
        painter.drawRect(QRectF(2.5, 5, 9, 14));
        painter.drawRect(QRectF(12.5, 5, 9, 14));
        break;
    }
    case ToolGlyph::CrosshairConfig: {
        // 十字线设置：十字线 + 四向手柄
        painter.drawLine(QPointF(12, 7), QPointF(12, 17));
        painter.drawLine(QPointF(7, 12), QPointF(17, 12));
        painter.drawRect(QRectF(10, 3, 4, 4));
        painter.drawRect(QRectF(10, 17, 4, 4));
        painter.drawRect(QRectF(3, 10, 4, 4));
        painter.drawRect(QRectF(17, 10, 4, 4));
        break;
    }
    case ToolGlyph::FullScreen: {
        // 全屏：四角括号
        painter.drawLine(QPointF(4, 9), QPointF(4, 4));
        painter.drawLine(QPointF(4, 4), QPointF(9, 4));
        painter.drawLine(QPointF(15, 4), QPointF(20, 4));
        painter.drawLine(QPointF(20, 4), QPointF(20, 9));
        painter.drawLine(QPointF(20, 15), QPointF(20, 20));
        painter.drawLine(QPointF(20, 20), QPointF(15, 20));
        painter.drawLine(QPointF(9, 20), QPointF(4, 20));
        painter.drawLine(QPointF(4, 20), QPointF(4, 15));
        break;
    }
    case ToolGlyph::Overlay: {
        // 叠加上游：两张错位叠放的图像
        painter.drawRect(QRectF(3, 3, 13, 13));
        painter.drawRect(QRectF(8, 8, 13, 13));
        break;
    }
    }
    return QIcon(pixmap);
}

}   // namespace

MainWindow::MainWindow(QWidget *parent, bool previewOnly) :
    QMainWindow(parent),
    ui(nullptr),
    m_executor(nullptr),
    m_imageView(nullptr),
    m_projectManager(nullptr),
    m_flowModeCombo(nullptr),
    m_selectedNode(nullptr),
    m_currentLanguage(Language::Chinese),
    m_previewOnly(previewOnly)
{
    VFP_DEBUG << "MainWindow constructor started";
    
    try {
        // 初始化UI
        VFP_DEBUG << "Creating Ui::MainWindow";
        ui = new Ui::MainWindow;
        if (!ui) {
            VFP_DEBUG << "Failed to create Ui::MainWindow";
            return;
        }
        
        VFP_DEBUG << "Calling setupUi";
        ui->setupUi(this);
    // 2026-10-30 r19 P1-3：UI 日志无上限，长跑内存与重绘持续增长
    ui->logTextEdit->setMaximumBlockCount(2000);

    // P2-3：日志栏默认只显示最近一条（VM 口径），单击「展开」查看全量
    {
        QSettings logSettings;
        m_logExpanded = logSettings.value(QStringLiteral("ui/logExpanded"), false).toBool();

        auto *logBar = new QWidget(ui->logDockContents);
        logBar->setObjectName(QStringLiteral("logBar"));
        auto *logBarLay = new QHBoxLayout(logBar);
        logBarLay->setContentsMargins(4, 2, 4, 2);
        logBarLay->setSpacing(6);
        auto *logHint = new QLabel(tr("最近日志"), logBar);
        logHint->setStyleSheet(QStringLiteral("color:#b0b0b0; font-size:12px;"));
        logBarLay->addWidget(logHint);
        logBarLay->addStretch();
        m_logExpandBtn = new QToolButton(logBar);
        m_logExpandBtn->setObjectName(QStringLiteral("logExpandButton"));
        m_logExpandBtn->setText(QStringLiteral("展开"));
        m_logExpandBtn->setToolTip(tr("展开查看完整日志（再次单击收起为最近一条）"));
        m_logExpandBtn->setAutoRaise(true);
        logBarLay->addWidget(m_logExpandBtn);

        if (auto *lay = qobject_cast<QVBoxLayout *>(ui->logDockContents->layout()))
            lay->insertWidget(0, logBar);

        connect(m_logExpandBtn, &QToolButton::clicked, this, [this]() {
            const bool next = !m_logExpanded;
            setLogExpanded(next);
            QSettings settings;
            settings.setValue(QStringLiteral("ui/logExpanded"), next);
        });
        setLogExpanded(m_logExpanded);
    }

        // 全局调色板已上移到 main.cpp 的 QApplication 创建之后（早于任何控件构造），
        // 这里只保留本窗口及子控件作用域的样式表，避免重复应用与深浅混杂。
        setStyleSheet(VisionWorkbenchStyle::globalWidgetsStylesheet());

        // === Dock widgets 布局设置 ===
        ui->flowTabs->setDocumentMode(true);
        // 辅助面板管理器：必须在 setupDockWidgets 之前创建（其末尾按上次显隐恢复面板）
        m_auxPanels = new AuxPanelManager(this, this);
        setupDockWidgets();

        // S2：流程名同名覆盖告警 → 状态栏提示（避免触发路由被静默重定向）
        connect(GlobalTriggerManager::instance(), &GlobalTriggerManager::flowNameCollision,
                this, [this](const QString &name) {
                    statusBar()->showMessage(
                        tr("警告：流程名 \"%1\" 已存在，触发路由可能被重定向").arg(name), 8000);
                });

        VFP_DEBUG << "setupUi completed";
        
        // VM 5.2.5：响应配置运行期常驻（构造函数即接上相机状态信号，后续方案载入/流程启停直接调用）
        CommResponseManager::instance();
        // === 实装 4.4：右侧图像区双页签「流程图像 / 模块结果」 ===
        // VM 把当前轮结果与图像放在同一区域、用页签切换；历史/帮助留在底部结果停靠面板。
        QWidget *imagePage = nullptr;
        {
            QVBoxLayout *dockLayout = qobject_cast<QVBoxLayout *>(ui->imageDockContents->layout());
            if (!dockLayout) {
                dockLayout = new QVBoxLayout(ui->imageDockContents);
            }
            dockLayout->setContentsMargins(0, 0, 0, 0);
            dockLayout->setSpacing(0);

            m_imageAreaTabs = new QTabWidget(ui->imageDockContents);
            // VM 页签按内容宽度排列（右侧留空），不把单个页签拉伸到整条页签栏
            m_imageAreaTabs->tabBar()->setExpanding(false);
            m_imageAreaTabs->setObjectName(QStringLiteral("imageAreaTabs"));
            m_imageAreaTabs->setDocumentMode(true);

            imagePage = new QWidget(m_imageAreaTabs);
            auto *imagePageLayout = new QVBoxLayout(imagePage);
            imagePageLayout->setContentsMargins(0, 0, 0, 0);
            imagePageLayout->setSpacing(0);
            m_imageAreaTabs->addTab(imagePage, tr("流程图像"));

            m_moduleResultPanel = new ResultTablePanel(m_imageAreaTabs);
            m_moduleResultPanel->setObjectName(QStringLiteral("moduleResultPanel"));
            // 图像区只看当前轮结果；历史/帮助由底部结果区承担（同一份数据两处翻页会分叉）
            m_moduleResultPanel->setPageVisibility(true, false, false);
            m_moduleResultPanel->setEmbeddedMode(true);
            m_imageAreaTabs->addTab(m_moduleResultPanel, tr("模块结果"));

            dockLayout->addWidget(m_imageAreaTabs, 1);
        }

        // 初始化图像视图
        VFP_DEBUG << "Creating HalconWindow";
        m_imageView = new HalconWindow(this);
        if (m_imageView && imagePage) {
            QVBoxLayout *layout = qobject_cast<QVBoxLayout*>(imagePage->layout());
            if (!layout) {
                layout = new QVBoxLayout(imagePage);
            }
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
            // 旧版 .ui 里的空 imageView 占位会抢走一半高度，必须先移除
            if (QWidget *placeholder = ui->imageDockContents->findChild<QWidget *>(QStringLiteral("imageView"))) {
                layout->removeWidget(placeholder);
                placeholder->hide();
                placeholder->deleteLater();
            }
            m_imageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            m_imageView->setMinimumSize(320, 240);
            layout->addWidget(m_imageView, 1);

            // 画布 ROI 绘制完成 → 写回测量节点参数
            connect(m_imageView, &HalconWindow::roiEdited, this, &MainWindow::handleRoiEdited);
            // 拖拽已有 ROI 的手柄同样要写回参数（roiEdited 只表示"画完新 ROI"）
            connect(m_imageView, &HalconWindow::roiHandleEdited, this, &MainWindow::handleRoiEdited);
            connect(m_imageView, &HalconWindow::maskEdited, this, &MainWindow::handleMaskEdited);

            // 恢复两条十字辅助线（实装 4.4 口径：像素坐标/线宽/颜色/锁定）
            {
                QSettings settings;
                QVector<HalconWindow::CrosshairConfig> configs(2);
                for (int i = 0; i < 2; ++i) {
                    const QString prefix = QStringLiteral("display/crosshair%1/").arg(i + 1);
                    HalconWindow::CrosshairConfig &cfg = configs[i];
                    cfg.visible = settings.value(prefix + QStringLiteral("enabled"), false).toBool();
                    cfg.x = settings.value(prefix + QStringLiteral("x"), -1.0).toDouble();
                    cfg.y = settings.value(prefix + QStringLiteral("y"), -1.0).toDouble();
                    cfg.thickness = qBound(1, settings.value(prefix + QStringLiteral("thickness"), 1).toInt(), 10);
                    cfg.locked = settings.value(prefix + QStringLiteral("locked"), false).toBool();
                    const QString defColor = (i == 0) ? QStringLiteral("#ff3c3c")
                                                      : QStringLiteral("#00c8ff");
                    cfg.color = QColor(settings.value(prefix + QStringLiteral("color"), defColor).toString());
                    if (!cfg.color.isValid())
                        cfg.color = QColor(defColor);
                }
                // 旧版单辅助线颜色迁移到第 1 条（位置是比例值，无图像时不强行换算，首帧自动居中）
                if (!settings.contains(QStringLiteral("display/crosshair1/color"))) {
                    QColor legacy(settings.value(QStringLiteral("display/crosshairColor"),
                                                 QStringLiteral("#ff3c3c")).toString());
                    if (legacy.isValid())
                        configs[0].color = legacy;
                }
                m_imageView->setCrosshairConfigs(configs);
            }
            
            // 添加图像来源提示
            m_imageSourceLabel = new QLabel(this);
            m_imageSourceLabel->setText(tr("图像来源: 无"));
            m_imageSourceLabel->setAlignment(Qt::AlignCenter);
            m_imageSourceLabel->setStyleSheet(VisionWorkbenchStyle::imageSourceStripStylesheet());
            m_imageSourceLabel->setFixedHeight(22);
            layout->addWidget(m_imageSourceLabel);

            // 图像显示路由与画布交互：显示决策（下拉框 > 画布选中 > 兜底）、叠加图元收集、
            // ROI 取点状态集中到这里；场景与画布选中通过 provider 注入，控制器不反向依赖主窗口。
            m_imageDisplay = new ImageDisplayController(m_imageView, m_imageSourceLabel, this);
            m_imageDisplay->setSceneProvider([this]() -> FlowScene * {
                const int idx = ui->flowTabs->currentIndex();
                return (idx >= 0 && idx < m_flowScenes.size()) ? m_flowScenes[idx] : nullptr;
            });
            m_imageDisplay->setCanvasSelectionProvider([this]() { return m_selectedNode; });
            // VM「结果显示 → 文本显示」的 {elapsed}/{结果键}：读执行器最近一次推送的结果快照
            // （耗时只存在于 vars 里，节点参数表没有 elapsedMs）。
            m_imageDisplay->setResultVarsProvider([this](NodeBase *node) -> QVariantMap {
                return node ? m_lastModuleVars.value(node->moduleId()) : QVariantMap();
            });

            // ---- 图像显示工具栏：适应 / 100% / 放大 / 缩小 / 十字线 ----
            {
                auto *imgBar = new QWidget(ui->imageDockContents);
                auto *imgBarLay = new QHBoxLayout(imgBar);
                imgBarLay->setContentsMargins(4, 2, 4, 2);
                imgBarLay->setSpacing(4);

                // 实装 4.4 图像区①区：图像源选择（当前模块 + 上游输出）
                auto *sourceLabel = new QLabel(tr("图像源"), imgBar);
                sourceLabel->setStyleSheet(QStringLiteral("color:#9fb3c8; font-size:12px;"));
                m_imageSourceCombo = new QComboBox(imgBar);
                m_imageSourceCombo->setObjectName(QStringLiteral("imgSourceCombo"));
                m_imageSourceCombo->setMinimumWidth(180);
                m_imageSourceCombo->setEnabled(false);
                imgBarLay->addWidget(sourceLabel);
                imgBarLay->addWidget(m_imageSourceCombo);
                connect(m_imageSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                        this, [this](int index) {
                            if (!m_imageSourceCombo || index < 0)
                                return;
                            NodeBase *selected =
                                m_imageSourceCombo->itemData(index).value<NodeBase *>();
                            if (!selected || !m_imageDisplay)
                                return;
                            const int flowIndex = ui->flowTabs->currentIndex();
                            if (flowIndex >= 0 && flowIndex < m_flowScenes.size())
                                m_imageDisplay->setSelectedOutputNode(m_flowScenes[flowIndex],
                                                                     selected);
                            m_imageDisplay->displayNodeOutput(selected);
                        });

                // 工具行改用 QToolBar：窗口/图像区变窄时自动收进“»”溢出菜单，
                // 不再像平铺按钮那样把图像区最小宽度撑到 700+（中央流程画布因此被挤成一条缝）。
                auto *imgTools = new QToolBar(ui->imageDockContents);
                imgTools->setObjectName(QStringLiteral("imageToolBar"));
                imgTools->setMovable(false);
                imgTools->setFloatable(false);
                imgTools->setToolButtonStyle(Qt::ToolButtonIconOnly);
                imgTools->setIconSize(QSize(18, 18));
                imgTools->setStyleSheet(QStringLiteral(
                    "QToolBar { background:#232333; border:none; spacing:2px; padding:2px 4px; }"
                    "QToolButton { background:transparent; border:none; border-radius:4px; padding:4px; }"
                    "QToolButton:hover { background:#2f2f45; }"
                    "QToolButton:pressed { background:#3a6ea5; }"
                    "QToolButton:checked { background:#2d4a66; }"));

                // VM 图像区资产实测（vm_main.png）：图标 16–18px、颜色 ≈#999797、笔画 ≈2px。
                // 尺寸 18px 已与实测一致；这里只把颜色与笔画向 VM 资产靠齐。
                const QColor imageToolInk(0x9a, 0x9a, 0x9a);
                auto addToolAct = [&](const QString &text, const char *objName,
                                      ToolGlyph glyph) {
                    QAction *a = imgTools->addAction(
                        toolbarGlyph(glyph, imageToolInk, 2.0), text);
                    a->setObjectName(QString::fromLatin1(objName));
                    return a;
                };
                // 顺序按 VM 实测（vm_imgtools_all.png）：适应 | 放大 缩小 1:1 弹出 | 其余扩展
                // （前五项与 VM 同序同组，扩展项放在第二条分隔线之后）
                QAction *fitAct = addToolAct(QStringLiteral("适应窗口"), "imgFitBtn", ToolGlyph::Fit);
                imgTools->addSeparator();
                QAction *zoomInAct = addToolAct(QStringLiteral("放大"), "imgZoomInBtn", ToolGlyph::ZoomIn);
                QAction *zoomOutAct = addToolAct(QStringLiteral("缩小"), "imgZoomOutBtn", ToolGlyph::ZoomOut);
                QAction *actualAct = addToolAct(QStringLiteral("100%"), "imgActualBtn", ToolGlyph::ActualSize);
                QAction *floatAct = addToolAct(QStringLiteral("弹出"), "imgFloatBtn", ToolGlyph::Float);
                imgTools->addSeparator();
                QAction *crossAct = addToolAct(QStringLiteral("十字线"), "imgCrossBtn", ToolGlyph::Crosshair);
                crossAct->setCheckable(true);
                QAction *saveImgAct = addToolAct(QStringLiteral("保存图像"), "imgSaveBtn", ToolGlyph::Save);
                QAction *dualAct = addToolAct(QStringLiteral("双画面"), "imgDualBtn", ToolGlyph::DualView);
                dualAct->setCheckable(true);
                QAction *crossCfgAct = addToolAct(QStringLiteral("十字线设置"), "imgCrosshairCfgBtn", ToolGlyph::CrosshairConfig);
                QAction *fullAct = addToolAct(QStringLiteral("全屏"), "imgFullScreenBtn", ToolGlyph::FullScreen);

                m_overlayUpstreamBtn = new QToolButton(imgTools);
                m_overlayUpstreamBtn->setObjectName(QStringLiteral("imgOverlayUpstreamBtn"));
                m_overlayUpstreamBtn->setText(QStringLiteral("叠加图像"));
                m_overlayUpstreamBtn->setIcon(
                    toolbarGlyph(ToolGlyph::Overlay, QColor(0x9a, 0x9a, 0x9a), 2.0));
                m_overlayUpstreamBtn->setIconSize(QSize(18, 18));
                m_overlayUpstreamBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
                m_overlayUpstreamBtn->setPopupMode(QToolButton::InstantPopup);
                m_overlayUpstreamBtn->setStyleSheet(QStringLiteral(
                    "QToolButton { background:transparent; border:none; border-radius:4px; padding:4px; }"
                    "QToolButton:hover { background:#2f2f45; }"
                    "QToolButton::menu-indicator { image:none; }"));
                imgTools->addWidget(m_overlayUpstreamBtn);
                imgBarLay->addStretch();

                // 实装 4.4：图像叠加透明度可配（此前固定 50%）
                // 透明度属于显示参数，放在上方「图像源」行右侧：工具条改成图标后
                // 不宜再塞自定义控件（窄 Dock 下会被裁掉，且控件不会进 » 溢出菜单）
                auto *opacityLabel = new QLabel(tr("叠加透明度"), imgBar);
                opacityLabel->setStyleSheet(QStringLiteral("color:#9fb3c8; font-size:12px;"));
                m_overlayOpacitySpin = new QSpinBox(imgBar);
                m_overlayOpacitySpin->setObjectName(QStringLiteral("imgOverlayOpacitySpin"));
                m_overlayOpacitySpin->setRange(0, 100);
                m_overlayOpacitySpin->setSuffix(QStringLiteral(" %"));
                QSettings opacitySettings;
                const int opacity = qBound(
                    0, opacitySettings.value(QStringLiteral("display/imageOverlayOpacity"), 50).toInt(), 100);
                m_overlayOpacitySpin->setValue(opacity);
                m_imageView->setHImageOverlayOpacity(opacity);
                imgBarLay->addWidget(opacityLabel);
                imgBarLay->addWidget(m_overlayOpacitySpin);
                connect(m_overlayOpacitySpin, QOverload<int>::of(&QSpinBox::valueChanged),
                        this, [this](int value) {
                            m_imageView->setHImageOverlayOpacity(value);
                            if (m_imageView2)
                                m_imageView2->setHImageOverlayOpacity(value);
                            QSettings settings;
                            settings.setValue(QStringLiteral("display/imageOverlayOpacity"), value);
                        });

                fitAct->setToolTip(QStringLiteral("\u81EA\u9002\u5E94\u7A97\u53E3\u5927\u5C0F"));
                actualAct->setToolTip(QStringLiteral("\u5B9E\u9645\u50CF\u7D20 100% \u663E\u793A"));
                zoomInAct->setToolTip(QStringLiteral("\u653E\u5927"));
                zoomOutAct->setToolTip(QStringLiteral("\u7F29\u5C0F"));
                crossAct->setToolTip(QStringLiteral("显示/隐藏十字辅助线"));
                saveImgAct->setToolTip(QStringLiteral("\u4FDD\u5B58\u5F53\u524D\u56FE\u50CF"));
                floatAct->setToolTip(QStringLiteral("\u5F39\u51FA\u4E3A\u72EC\u7ACB\u7A97\u53E3\uFF0C\u53EF\u62D6\u5927\uFF1B\u518D\u70B9\u8FD8\u539F"));
                dualAct->setToolTip(QStringLiteral("双画面：增加一路可独立缩放的图像视图"));
                crossCfgAct->setToolTip(QStringLiteral("配置十字辅助线位置与颜色"));
                fullAct->setToolTip(QStringLiteral("全屏查看当前图像（Esc 退出）"));

                // P2-5：副视图（默认隐藏；双画面切换时显示）
                m_imageView2 = new HalconWindow(ui->imageDockContents);
                m_imageView2->setObjectName(QStringLiteral("imageViewSecondary"));
                m_imageView2->setVisible(false);
                layout->insertWidget(layout->indexOf(m_imageView) + 1, m_imageView2, 1);
                if (m_overlayOpacitySpin)
                    m_imageView2->setHImageOverlayOpacity(m_overlayOpacitySpin->value());
                // 双画面语义：左=当前模块输入源，右=完整结果（由显示控制器统一分流）
                if (m_imageDisplay)
                    m_imageDisplay->setSecondaryView(m_imageView2);

                connect(fitAct, &QAction::triggered, m_imageView, &HalconWindow::fitToWindow);
                connect(actualAct, &QAction::triggered, m_imageView, &HalconWindow::setActualSize);
                connect(zoomInAct, &QAction::triggered, m_imageView, &HalconWindow::zoomIn);
                connect(zoomOutAct, &QAction::triggered, m_imageView, &HalconWindow::zoomOut);
                connect(crossAct, &QAction::toggled, m_imageView, &HalconWindow::setCrosshairVisible);
                connect(saveImgAct, &QAction::triggered, m_imageView, &HalconWindow::saveImage);
                connect(floatAct, &QAction::triggered, this, &MainWindow::onToggleImageFloat);
                connect(dualAct, &QAction::toggled, this, [this](bool on) {
                    if (!m_imageView2)
                        return;
                    m_imageView2->setVisible(on);
                    if (m_imageDisplay)
                        m_imageDisplay->setDualViewEnabled(on);
                    m_imageView2->setCrosshairConfigs(m_imageView->crosshairConfigs());
                    // 切换后按新语义重放一次显示（单画面=主视图完整结果；双画面=左输入/右结果）
                    NodeBase *node = m_imageDisplay
                                         ? m_imageDisplay->resolveDisplayNode(m_selectedNode)
                                         : nullptr;
                    if (m_imageDisplay && node)
                        m_imageDisplay->displayNodeOutput(node);
                });
                connect(crossCfgAct, &QAction::triggered, this,
                        &MainWindow::showCrosshairSettings);
                connect(fullAct, &QAction::triggered, this,
                        &MainWindow::showFullScreenImage);

                // P2-5：双画面由 ImageDisplayController 分流（左输入/右结果），不再镜像同步。
                // 辅助线位置/线宽变化（含拖动）→ 副视图只同步配置，不重载图像
                connect(m_imageView, &HalconWindow::crosshairConfigsChanged, this, [this]() {
                    if (m_imageView2 && m_imageView2->isVisible())
                        m_imageView2->setCrosshairConfigs(m_imageView->crosshairConfigs());
                });

                // 叠加上游：勾选上游模块后，其 ROI/测量图元叠加显示在当前画面上
                {
                    QMenu *overlayMenu = new QMenu(m_overlayUpstreamBtn);
                    connect(overlayMenu, &QMenu::aboutToShow, this, [this, overlayMenu]() {
                        overlayMenu->clear();
                        NodeBase *displayNode = resolveDisplayNode(m_selectedNode);
                        if (!displayNode) {
                            overlayMenu->addAction(tr("当前没有可叠加的模块"))->setEnabled(false);
                            return;
                        }
                        // 实装 4.4：当前模块与上游模块的图像都可多选叠加
                        QList<NodeBase *> candidates;
                        candidates << displayNode;
                        for (NodeBase *up : upstreamNodes(displayNode)) {
                            if (up && up != displayNode)
                                candidates << up;
                        }
                        for (NodeBase *node : candidates) {
                            const QString label = (node == displayNode)
                                ? tr("%1（当前模块）").arg(node->fullName())
                                : node->fullName();
                            QAction *act = overlayMenu->addAction(label);
                            act->setCheckable(true);
                            act->setChecked(m_overlayNodes.contains(node));
                            connect(act, &QAction::toggled, this, [this, node, displayNode](bool on) {
                                if (on)
                                    m_overlayNodes.insert(node);
                                else
                                    m_overlayNodes.remove(node);
                                applyExtraOverlays(displayNode);
                            });
                        }
                    });
                    m_overlayUpstreamBtn->setMenu(overlayMenu);
                    m_overlayUpstreamBtn->setToolTip(
                        tr("多选当前模块/上游模块的输出图像，叠加显示在预览画面上"));
                }

                // VM 图像工具条只保留 5 枚主图标（适应 | 放大 缩小 1:1 弹出）；十字线/保存/双画面/
                // 十字线设置/全屏收进「更多」菜单，避免 11 枚图标平铺占满图像区顶部（实测 VM 同区仅 5 枚）。
                {
                    QMenu *moreMenu = new QMenu(imgTools);
                    moreMenu->addAction(crossAct);
                    moreMenu->addAction(saveImgAct);
                    moreMenu->addAction(dualAct);
                    moreMenu->addAction(crossCfgAct);
                    moreMenu->addAction(fullAct);
                    for (QAction *act : {crossAct, saveImgAct, dualAct, crossCfgAct, fullAct})
                        imgTools->removeAction(act);

                    auto *moreBtn = new QToolButton(imgTools);
                    moreBtn->setObjectName(QStringLiteral("imgMoreBtn"));
                    moreBtn->setIcon(toolbarGlyph(ToolGlyph::More, imageToolInk, 2.0));
                    moreBtn->setIconSize(QSize(18, 18));
                    moreBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
                    moreBtn->setPopupMode(QToolButton::InstantPopup);
                    moreBtn->setToolTip(tr("更多视图操作（十字线 / 保存 / 双画面 / 全屏）"));
                    moreBtn->setStyleSheet(QStringLiteral(
                        "QToolButton { background:transparent; border:none; border-radius:4px; padding:4px; }"
                        "QToolButton:hover { background:#2f2f45; }"
                        "QToolButton::menu-indicator { image:none; }"));
                    moreBtn->setMenu(moreMenu);
                    imgTools->addWidget(moreBtn);
                }

                // 工具条单独一行：合并进「图像源」行时 QHBoxLayout 会挤压下拉框并把右端图标裁掉
                //（实测 1366 宽下图像源下拉被压到箭头大小）。VM 的单行形态在窄 Dock 下不可靠，
                // 保留两行 + QToolBar 自身 » 溢出更稳。
                layout->insertWidget(0, imgBar, 0);
                layout->insertWidget(1, imgTools, 0);
                layout->setStretch(layout->indexOf(m_imageView), 1);
                if (m_imageSourceLabel)
                    layout->setStretch(layout->indexOf(m_imageSourceLabel), 0);
            }

            // ---- P2-6：图像区「本地图④区」----
            // 官方口径把本地图列表放在图像显示区；选中 ImageReadNode 时联动显示，
            // 提供 添加文件夹 / 全部删除 / 首张 / 运行选中 / 运行全部。
            {
                m_localImageGroup = new QGroupBox(tr("本地图"), ui->imageDockContents);
                m_localImageGroup->setObjectName(QStringLiteral("localImageGroup"));
                m_localImageGroup->setMaximumHeight(180);
                auto *localLay = new QVBoxLayout(m_localImageGroup);
                localLay->setContentsMargins(4, 4, 4, 4);
                localLay->setSpacing(3);

                // 预览导航：预览窗口左右箭头（这里放在本地图区顶部，等价切换当前图）
                auto *navRow = new QHBoxLayout();
                navRow->setSpacing(3);
                m_localPrevBtn = new QToolButton(m_localImageGroup);
                m_localPrevBtn->setObjectName(QStringLiteral("localPrevBtn"));
                m_localPrevBtn->setText(QStringLiteral("◀"));
                m_localPrevBtn->setToolTip(tr("上一张本地图"));
                m_localNextBtn = new QToolButton(m_localImageGroup);
                m_localNextBtn->setObjectName(QStringLiteral("localNextBtn"));
                m_localNextBtn->setText(QStringLiteral("▶"));
                m_localNextBtn->setToolTip(tr("下一张本地图"));
                m_localIndexLabel = new QLabel(QStringLiteral("0 / 0"), m_localImageGroup);
                m_localIndexLabel->setAlignment(Qt::AlignCenter);
                m_localIndexLabel->setStyleSheet(QStringLiteral("color:#8f9aa8; font-size:12px;"));
                navRow->addWidget(m_localPrevBtn);
                navRow->addWidget(m_localIndexLabel, 1);
                navRow->addWidget(m_localNextBtn);
                localLay->addLayout(navRow);

                m_localImageList = new QListWidget(m_localImageGroup);
                m_localImageList->setObjectName(QStringLiteral("localImageList"));
                m_localImageList->setAlternatingRowColors(true);
                m_localImageList->setMaximumHeight(110);
                localLay->addWidget(m_localImageList);

                auto *btnRow = new QHBoxLayout();
                btnRow->setSpacing(3);
                auto makeLocalBtn = [&](const QString &text, const char *name) {
                    auto *b = new QPushButton(text, m_localImageGroup);
                    b->setObjectName(QString::fromLatin1(name));
                    b->setStyleSheet(QStringLiteral(
                        "QPushButton { background:#2b2b3d; color:#d8d8d8; border:1px solid #4a4a6a;"
                        " border-radius:3px; padding:2px 8px; font-size:12px; }"
                        "QPushButton:hover { background:#3a3a55; }"
                        "QPushButton:disabled { color:#666; border-color:#3a3a4a; }"));
                    btnRow->addWidget(b);
                    return b;
                };
                QPushButton *addImageBtn = makeLocalBtn(tr("添加图片"), "localAddImageBtn");
                QPushButton *addFolderBtn = makeLocalBtn(tr("添加文件夹"), "localAddFolderBtn");
                QPushButton *clearBtn = makeLocalBtn(tr("全部删除"), "localClearBtn");
                QPushButton *firstBtn = makeLocalBtn(tr("首张"), "localFirstBtn");
                QPushButton *runSelBtn = makeLocalBtn(tr("运行选中"), "localRunSelectedBtn");
                QPushButton *runAllBtn = makeLocalBtn(tr("运行全部"), "localRunAllBtn");
                m_localAutoSwitch = new QCheckBox(tr("自动切换"), m_localImageGroup);
                m_localAutoSwitch->setObjectName(QStringLiteral("localAutoSwitch"));
                m_localAutoSwitch->setToolTip(tr("连续执行时自动切换到下一张本地图"));
                btnRow->addWidget(m_localAutoSwitch);
                btnRow->addStretch();
                localLay->addLayout(btnRow);
                layout->addWidget(m_localImageGroup, 0);
                m_localImageGroup->setVisible(false);

                // 实装 4.4：添加单张/多张本地图 + 自动切换 + 预览左右切换
                connect(addImageBtn, &QPushButton::clicked, this, [this]() {
                    if (!m_localImageNode)
                        return;
                    const QString startDir =
                        m_localImageNode->getParam(QStringLiteral("filePath")).toString();
                    const QStringList files = QFileDialog::getOpenFileNames(
                        this, tr("选择本地图像"), startDir,
                        tr("图像文件 (*.bmp *.jpg *.jpeg *.png *.tif *.tiff);;所有文件 (*.*)"));
                    if (files.isEmpty())
                        return;
                    if (!m_localImageNode->addLocalImageFiles(files)) {
                        logMessage(tr("添加本地图失败：文件不可读或列表未变化"));
                        return;
                    }
                    refreshLocalImagePanel(m_localImageNode);
                    logMessage(tr("已添加 %1 张本地图").arg(files.size()));
                });
                connect(m_localAutoSwitch, &QCheckBox::toggled, this, [this](bool on) {
                    if (!m_localImageNode)
                        return;
                    m_localImageNode->setParam(QStringLiteral("autoSwitch"), on);
                });
                auto stepLocalImage = [this](int delta) {
                    if (!m_localImageNode)
                        return;
                    const int count = m_localImageNode->localImageFiles().size();
                    if (count <= 0)
                        return;
                    int index = m_localImageNode->currentLocalImageIndex();
                    if (index < 0 || index >= count)
                        index = (delta > 0) ? -1 : 0;
                    const int next = (index + delta + count) % count;
                    m_localImageNode->setLocalImageIndex(next);
                    executeNodeOnce(m_localImageNode);
                    refreshLocalImagePanel(m_localImageNode);
                };
                connect(m_localPrevBtn, &QToolButton::clicked, this,
                        [stepLocalImage]() { stepLocalImage(-1); });
                connect(m_localNextBtn, &QToolButton::clicked, this,
                        [stepLocalImage]() { stepLocalImage(1); });

                connect(addFolderBtn, &QPushButton::clicked, this, [this]() {
                    if (!m_localImageNode)
                        return;
                    const QString dir = QFileDialog::getExistingDirectory(
                        this, tr("选择本地图文件夹"),
                        m_localImageNode->getParam(QStringLiteral("filePath")).toString());
                    if (dir.isEmpty())
                        return;
                    m_localImageNode->setParam(QStringLiteral("mode"),
                                               static_cast<int>(ImageReadNode::Mode::MultiImage));
                    m_localImageNode->setParam(QStringLiteral("filePath"), dir);
                    refreshLocalImagePanel(m_localImageNode);
                    logMessage(tr("本地图文件夹已设置：%1").arg(dir));
                });
                connect(clearBtn, &QPushButton::clicked, this, [this]() {
                    if (!m_localImageNode)
                        return;
                    m_localImageNode->clearLocalImages();
                    refreshLocalImagePanel(m_localImageNode);
                    logMessage(tr("已清空本地图列表"));
                });
                connect(firstBtn, &QPushButton::clicked, this, [this]() {
                    if (!m_localImageNode || m_localImageNode->localImageFiles().isEmpty())
                        return;
                    m_localImageNode->setLocalImageIndex(0);
                    executeNodeOnce(m_localImageNode);
                    refreshLocalImagePanel(m_localImageNode);
                });
                connect(runSelBtn, &QPushButton::clicked, this, [this]() {
                    if (!m_localImageNode || !m_localImageList)
                        return;
                    const int row = m_localImageList->currentRow();
                    if (row < 0)
                        return;
                    m_localImageNode->setLocalImageIndex(row);
                    executeNodeOnce(m_localImageNode);
                    refreshLocalImagePanel(m_localImageNode);
                });
                connect(runAllBtn, &QPushButton::clicked, this, [this]() {
                    if (!m_localImageNode || m_localImageNode->localImageFiles().isEmpty())
                        return;
                    const int count = m_localImageNode->localImageFiles().size();
                    if (count > 200
                        && QMessageBox::question(this, tr("运行全部"),
                                                 tr("当前共 %1 张图，逐张运行可能耗时较长，继续？")
                                                     .arg(count))
                               != QMessageBox::Yes)
                        return;
                    bool allOk = true;
                    for (int i = 0; i < count; ++i) {
                        if (!m_localImageNode)
                            break;
                        m_localImageNode->setLocalImageIndex(i);
                        allOk = m_localImageNode->execute() && allOk;
                        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                    }
                    if (m_localImageNode && m_imageView)
                        m_imageDisplay->displayNodeOutput(m_localImageNode);
                    refreshLocalImagePanel(m_localImageNode);
                    logMessage(allOk ? tr("本地图全部运行完成（%1 张）").arg(count)
                                     : tr("本地图运行结束，存在失败帧（%1 张）").arg(count));
                });
                connect(m_localImageList, &QListWidget::itemDoubleClicked, this,
                        [this](QListWidgetItem *item) {
                            if (!m_localImageNode || !item)
                                return;
                            const int row = m_localImageList->row(item);
                            m_localImageNode->setLocalImageIndex(row);
                            executeNodeOnce(m_localImageNode);
                            refreshLocalImagePanel(m_localImageNode);
                        });
            }

            // 只给「流程图像」页上底色：模块结果页沿用全局主题（否则树/按钮会被扁平化）
            imagePage->setStyleSheet(QStringLiteral(
                "QWidget { background-color: #252528; border: none; }"));
            VFP_DEBUG << "HalconWindow created and added to dock";
        }

        // 初始化执行器
        VFP_DEBUG << "Creating FlowExecutor";
        // 词条切片 C P3b 期（销毁权归一）：新建即建令牌——令牌接管本体的所有权（并解除父子），
        // 本体的删除只有令牌件这一处；界面侧此后只借出裸指针（token->executor()）。
        m_flowExecutors.insert(nullptr, FlowExecutorToken::create(new FlowExecutor()));
        m_executor = m_flowExecutors.value(nullptr)->executor();  // 占位：首个流程创建时复用
        // 应用失败中断策略（setupDockWidgets 早于此处调用，故在此真正生效）
        {
            QSettings settings;
            m_executor->setStopOnFailure(
                settings.value(QStringLiteral("flow/stopOnFailure"), true).toBool());
        }
        // 启动恢复阶段可能已按上次的显隐把性能面板打开（那时 m_executor 还不存在），
        // 这里补一次绑定，否则面板会一直是空表。
        if (auto *pp = m_auxPanels->performancePanel()) {
            pp->bindExecutor(m_executor);
        }

        VFP_DEBUG << "FlowExecutor created";

        // ── 顶部快捷工具条（VM 4.4 口径：单行图标 + 悬停提示；右侧保留运行模式与方案信息）──
        {
            QToolBar *mainToolBar = addToolBar(tr("流程控制"));
            mainToolBar->setObjectName("mainToolBar");
            mainToolBar->setMovable(false);

            // ── 实装 4.4：快捷工具条图标化（VM：单行图标 + 悬停提示）──
            // 工具条用「图标代理动作」：图标只出现在工具条，菜单保持纯文字外观；
            // 代理动作的 text/enabled 跟随源动作，权限收口与运行态禁用不会分叉。
            mainToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
            mainToolBar->setIconSize(QSize(22, 22));
            mainToolBar->setStyleSheet(
                "QToolBar { background-color:#1e1e2e; border-bottom:1px solid #3a3a5c;"
                " spacing:2px; padding:2px 8px; }"
                "QToolBar::separator { background:#3a3a4e; width:1px; margin:4px 6px; }"
                "QToolButton { background:transparent; border:none; border-radius:4px; padding:4px; }"
                "QToolButton:hover { background:#2f2f45; }"
                "QToolButton:pressed { background:#3a6ea5; }"
                "QToolButton:checked { background:#2d4a66; }");

            auto addToolbarProxy = [this, mainToolBar](QAction *source, const QIcon &icon,
                                                       const QString &toolTip) {
                if (!source)
                    return;
                QAction *proxy = new QAction(icon, source->text(), this);
                proxy->setObjectName(QStringLiteral("toolbar_") + source->objectName());
                proxy->setToolTip(toolTip);
                proxy->setEnabled(source->isEnabled());
                connect(proxy, &QAction::triggered, source, &QAction::trigger);
                connect(source, &QAction::changed, proxy, [source, proxy]() {
                    QString text = source->text();
                    text.remove(QLatin1Char('&'));
                    proxy->setText(text);
                    proxy->setEnabled(source->isEnabled());
                });
                mainToolBar->addAction(proxy);
            };

            // 文件组（VM 工具条前段：保存 / 打开，新建紧随其后）
            addToolbarProxy(ui->actionSaveScheme, toolbarGlyph(ToolGlyph::Save),
                            tr("保存当前方案（Ctrl+S）"));
            addToolbarProxy(ui->actionOpenScheme, toolbarGlyph(ToolGlyph::Open),
                            tr("打开方案（Ctrl+O）"));
            addToolbarProxy(ui->actionNew, toolbarGlyph(ToolGlyph::New),
                            tr("新建方案（Ctrl+N）"));
            mainToolBar->addSeparator();

            // 编辑组：撤销 / 重做（带历史下拉）/ 锁定
            m_undoAction = new QAction(tr("撤销"), this);
            m_undoAction->setObjectName("undoAction");
            m_undoAction->setIcon(toolbarGlyph(ToolGlyph::Undo));
            m_undoAction->setToolTip(tr("撤销当前流程的上一步操作（Ctrl+Z）；点右侧箭头可选择回退到哪一步"));
            connect(m_undoAction, &QAction::triggered, this, [this]() {
                if (FlowScene *scene = currentFlowScene())
                    scene->undo();
            });

            m_redoAction = new QAction(tr("重做"), this);
            m_redoAction->setObjectName("redoAction");
            m_redoAction->setIcon(toolbarGlyph(ToolGlyph::Redo));
            m_redoAction->setToolTip(tr("重做当前流程的下一步操作（Ctrl+Y）；点右侧箭头可查看重做历史"));
            connect(m_redoAction, &QAction::triggered, this, [this]() {
                if (FlowScene *scene = currentFlowScene())
                    scene->redo();
            });

            // P2-9：撤销/重做按钮带历史下拉（VM 口径：按钮 + 历史列表，而非只有快捷键）
            {
                auto *undoBtn = new QToolButton(mainToolBar);
                undoBtn->setObjectName(QStringLiteral("undoHistoryButton"));
                undoBtn->setDefaultAction(m_undoAction);
                undoBtn->setPopupMode(QToolButton::MenuButtonPopup);
                undoBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
                undoBtn->setIconSize(QSize(22, 22));
                auto *undoMenu = new QMenu(undoBtn);
                connect(undoMenu, &QMenu::aboutToShow, this, [this, undoMenu]() {
                    undoMenu->clear();
                    FlowScene *scene = currentFlowScene();
                    const QStringList texts = scene ? scene->undoHistoryTexts() : QStringList();
                    if (texts.isEmpty()) {
                        undoMenu->addAction(tr("没有可撤销的操作"))->setEnabled(false);
                        return;
                    }
                    for (int i = 0; i < texts.size(); ++i) {
                        QAction *act = undoMenu->addAction(texts.at(i));
                        connect(act, &QAction::triggered, this, [this, i]() {
                            FlowScene *s = currentFlowScene();
                            if (!s)
                                return;
                            for (int step = 0; step <= i; ++step)
                                s->undo();
                        });
                    }
                });
                undoBtn->setMenu(undoMenu);
                mainToolBar->addWidget(undoBtn);

                auto *redoBtn = new QToolButton(mainToolBar);
                redoBtn->setObjectName(QStringLiteral("redoHistoryButton"));
                redoBtn->setDefaultAction(m_redoAction);
                redoBtn->setPopupMode(QToolButton::MenuButtonPopup);
                redoBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
                redoBtn->setIconSize(QSize(22, 22));
                auto *redoMenu = new QMenu(redoBtn);
                connect(redoMenu, &QMenu::aboutToShow, this, [this, redoMenu]() {
                    redoMenu->clear();
                    FlowScene *scene = currentFlowScene();
                    const QStringList texts = scene ? scene->redoHistoryTexts() : QStringList();
                    if (texts.isEmpty()) {
                        redoMenu->addAction(tr("没有可重做的操作"))->setEnabled(false);
                        return;
                    }
                    for (int i = 0; i < texts.size(); ++i) {
                        QAction *act = redoMenu->addAction(texts.at(i));
                        connect(act, &QAction::triggered, this, [this, i]() {
                            FlowScene *s = currentFlowScene();
                            if (!s)
                                return;
                            for (int step = 0; step <= i; ++step)
                                s->redo();
                        });
                    }
                });
                redoBtn->setMenu(redoMenu);
                mainToolBar->addWidget(redoBtn);
            }

            m_lockCanvasAction = mainToolBar->addAction(tr("锁定"));
            m_lockCanvasAction->setObjectName("lockCanvasAction");
            m_lockCanvasAction->setIcon(toolbarGlyph(ToolGlyph::Lock));
            m_lockCanvasAction->setCheckable(true);
            m_lockCanvasAction->setToolTip(tr("显式锁定当前流程的画布编辑（与运行期自动锁互不覆盖）"));
            connect(m_lockCanvasAction, &QAction::triggered, this, [this](bool on) {
                FlowScene *scene = currentFlowScene();
                if (!scene) {
                    m_lockCanvasAction->setChecked(false);
                    return;
                }
                if (on) {
                    const auto answer = QMessageBox::question(
                        this, tr("锁定流程"),
                        tr("锁定后当前流程无法移动/增删模块，确定锁定「%1」吗？").arg(scene->flowName()),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                    if (answer != QMessageBox::Yes) {
                        m_lockCanvasAction->setChecked(false);
                        return;
                    }
                }
                scene->setUserLocked(on);
                logMessage(on ? tr("流程已锁定：%1").arg(scene->flowName())
                              : tr("流程已解锁：%1").arg(scene->flowName()));
            });
            mainToolBar->addSeparator();

            // 配置入口组：相机 / 变量 / 通讯 / 触发 / 脚本
            addToolbarProxy(ui->actionCameraConfig, toolbarGlyph(ToolGlyph::Camera),
                            tr("相机管理"));
            addToolbarProxy(ui->actionGlobalVariables, toolbarGlyph(ToolGlyph::Variables),
                            tr("全局变量"));
            addToolbarProxy(ui->actionCommDevice, toolbarGlyph(ToolGlyph::Comm),
                            tr("通信管理"));
            addToolbarProxy(ui->actionGlobalTrigger, toolbarGlyph(ToolGlyph::Trigger),
                            tr("全局触发"));
            addToolbarProxy(ui->actionCodeExport, toolbarGlyph(ToolGlyph::Script),
                            tr("导出算子代码"));
            mainToolBar->addSeparator();

            // 运行控制组（VM 口径：开始/停止为图标，单次/单步/暂停紧随其后）
            addToolbarProxy(ui->actionStartExecution, toolbarGlyph(ToolGlyph::Start),
                            tr("开始执行（F5）"));
            addToolbarProxy(ui->actionStopExecution, toolbarGlyph(ToolGlyph::Stop),
                            tr("停止执行（Shift+F5）"));
            // 开始/停止的快捷键挂在源动作上；源动作不再直接进工具条，必须显式登记到窗口，
            // 否则 F5 / Shift+F5 会随动作一起失效。
            addAction(ui->actionStartExecution);
            addAction(ui->actionStopExecution);

            // 单次执行按钮：软触发模式下运行一次，不影响流程模式设置
            m_singleShotBtn = new QToolButton();
            m_singleShotBtn->setObjectName("singleShotBtn");
            m_singleShotBtn->setText(tr("单次执行"));
            m_singleShotBtn->setIcon(toolbarGlyph(ToolGlyph::SingleShot));
            m_singleShotBtn->setIconSize(QSize(22, 22));
            m_singleShotBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            m_singleShotBtn->setToolTip(tr("以软触发方式运行当前流程一次"));
            mainToolBar->addWidget(m_singleShotBtn);
            connect(m_singleShotBtn, &QToolButton::clicked, this, &MainWindow::onSingleShotExecution);

            // 单步执行按钮：每执行一个节点后暂停（步进模式）
            m_stepBtn = new QToolButton();
            m_stepBtn->setObjectName("stepBtn");
            m_stepBtn->setText(tr("单步"));
            m_stepBtn->setIcon(toolbarGlyph(ToolGlyph::Step));
            m_stepBtn->setIconSize(QSize(22, 22));
            m_stepBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            m_stepBtn->setToolTip(tr("逐节点执行：每运行一个算子后暂停（可配合单次执行排查流程）"));
            mainToolBar->addWidget(m_stepBtn);
            connect(m_stepBtn, &QToolButton::clicked, this, [this]() {
                if (!m_executor)
                    return;
                // 173 行拍板：闸开不发新跑——空闲/停止态的步进会从首个节点新起一轮，随闸收；
                // 运行中/暂停下的步进是运行态控制（标记节点断点、继续），不在闸内。
                const ExecutionState estate = m_executor->getState();
                if ((estate == ExecutionState::Idle || estate == ExecutionState::Stopped)
                    && !SessionManager::instance()->canRunFlow()) {
                    logMessage(tr("写闸开启期间不受理步进起跑：出厂默认口令仍在使用（改密后恢复）"));
                    return;
                }
                m_executor->stepExecution();
            });

            // 暂停/继续按钮：运行中→暂停；暂停中→继续（图标随状态切换，见 ExecutionStatusController）
            m_pauseBtn = new QToolButton();
            m_pauseBtn->setObjectName("pauseBtn");
            m_pauseBtn->setText(tr("暂停"));
            m_pauseBtn->setIcon(toolbarGlyph(ToolGlyph::Pause));
            m_pauseBtn->setIconSize(QSize(22, 22));
            m_pauseBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            m_pauseBtn->setToolTip(tr("暂停当前流程（不清输入缓存）；再按继续"));
            mainToolBar->addWidget(m_pauseBtn);
            connect(m_pauseBtn, &QToolButton::clicked, this, &MainWindow::onPauseResumeExecution);
            mainToolBar->addSeparator();

            // 运行模式选择器（VM 右上「运行模式」；这里保留下拉，切换仍即时生效）
            QLabel *modeLabel = new QLabel(QStringLiteral(" 流程模式: "));
            modeLabel->setStyleSheet("color: #c0c0c0; font-size: 13px;");
            mainToolBar->addWidget(modeLabel);

            m_flowModeCombo = new QComboBox();
            m_flowModeCombo->setObjectName("flowModeCombo");
            m_flowModeCombo->addItem(tr("连续模式"),    static_cast<int>(FlowMode::Continuous));
            m_flowModeCombo->addItem(tr("软触发模式"),  static_cast<int>(FlowMode::SoftwareTrigger));
            m_flowModeCombo->addItem(tr("硬触发模式"),  static_cast<int>(FlowMode::HardwareTrigger));
            m_flowModeCombo->setCurrentIndex(1); // 默认软触发模式
            m_flowModeCombo->setMinimumWidth(130);
            m_flowModeCombo->setToolTip(QStringLiteral(
                "连续模式：切换到该模式后自动循环执行流程\n"
                "软触发模式：需手动点击「开始执行」运行一次流程\n"
                "硬触发模式：仅相机硬件触发时自动执行一次"
            ));
            m_flowModeCombo->setStyleSheet(
                "QComboBox {"
                "  background-color: #2b2b3d; color: #e0e0e0;"
                "  border: 1px solid #4a6a9c; border-radius: 4px;"
                "  padding: 3px 10px; min-height: 24px;"
                "  font-size: 13px;"
                "}"
                "QComboBox::drop-down { border: none; width: 22px; }"
                "QComboBox::down-arrow { image: none; }"
                "QComboBox QAbstractItemView {"
                "  background-color: #2b2b3d; color: #e0e0e0;"
                "  selection-background-color: #3a6ea5;"
                "  border: 1px solid #4a6a9c;"
                "}"
            );
            mainToolBar->addWidget(m_flowModeCombo);
            mainToolBar->addSeparator();

            // 综合入口组：综合配置 / 运行界面 / 参数查找
            {
                QAction *comprehensiveAct = mainToolBar->addAction(tr("综合配置"));
                comprehensiveAct->setObjectName(QStringLiteral("comprehensiveConfigAction"));
                comprehensiveAct->setIcon(toolbarGlyph(ToolGlyph::Config));
                comprehensiveAct->setToolTip(tr("集中配置：相机 / 通讯 / 触发 / 用户 / 数据与报表"));
                connect(comprehensiveAct, &QAction::triggered, this,
                        &MainWindow::openComprehensiveConfig);
            }
            addToolbarProxy(ui->actionRuntimeDesigner, toolbarGlyph(ToolGlyph::RuntimeUi),
                            tr("运行界面设计"));
            if (ui->actionParameterSearch) {
                addToolbarProxy(ui->actionParameterSearch, toolbarGlyph(ToolGlyph::Search),
                                tr("参数查找"));
            }

            // P1-8：用户名/角色切换入口（保留登录审计，运行中可切换或注销）
            m_userButton = new QToolButton();
            m_userButton->setObjectName("userButton");
            m_userButton->setPopupMode(QToolButton::InstantPopup);
            m_userButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
            m_userButton->setStyleSheet(
                "QToolButton {"
                "  background-color: #2b2b3d; color: #e0e0e0;"
                "  border: 1px solid #4a6a9c; border-radius: 4px;"
                "  padding: 3px 10px; min-height: 24px; font-size: 13px;"
                "}"
                "QToolButton:hover { background-color: #3a3a55; }"
            );
            {
                QMenu *userMenu = new QMenu(m_userButton);
                userMenu->addAction(tr("切换用户…"), this, &MainWindow::switchUser);
                userMenu->addAction(tr("注销"), this, &MainWindow::logoutUser);
                userMenu->addSeparator();
                userMenu->addAction(ui->actionUserManagement);
                m_userButton->setMenu(userMenu);
            }
            mainToolBar->addWidget(m_userButton);

            // P2-2：顶栏右侧显示当前方案名 + 资源占用（对标 VM 顶部信息条）
            {
                auto *spacer = new QWidget(mainToolBar);
                spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                mainToolBar->addWidget(spacer);

                m_schemeLabel = new QLabel(mainToolBar);
                m_schemeLabel->setObjectName(QStringLiteral("schemeNameLabel"));
                m_schemeLabel->setStyleSheet(
                    QStringLiteral("QLabel { color:#d8d8d8; font-size:13px; padding:0 8px; }"));
                mainToolBar->addWidget(m_schemeLabel);

                m_resourceLabel = new QLabel(mainToolBar);
                m_resourceLabel->setObjectName(QStringLiteral("resourceUsageLabel"));
                m_resourceLabel->setStyleSheet(
                    QStringLiteral("QLabel { color:#9fb3c8; font-size:12px; padding:0 8px; }"));
                m_resourceLabel->setToolTip(tr("系统资源占用：CPU / 内存 / 方案所在磁盘"));
                mainToolBar->addWidget(m_resourceLabel);

                updateSchemeLabel();
                // 运行策略（VM 4.4 手册 11.4）：诊断模式持久化
                m_resourceDiagnosticMode =
                    QSettings().value(QStringLiteral("flow/resourceDiagnosticMode"), false).toBool();
                m_resourceTimer = new QTimer(this);
                m_resourceTimer->setInterval(2000);
                connect(m_resourceTimer, &QTimer::timeout, this, &MainWindow::refreshResourceUsage);
                m_resourceTimer->start();
                refreshResourceUsage();

                // VM 顶部右侧「运行模式」：一键切换到自定义运行界面（操作员模式）。
                // 用源动作做默认动作，可用态/文案跟随 actionRuntimeMode，不另建规则。
                {
                    auto *runModeBtn = new QToolButton(mainToolBar);
                    runModeBtn->setObjectName(QStringLiteral("runModeButton"));
                    runModeBtn->setDefaultAction(ui->actionRuntimeMode);
                    runModeBtn->setIcon(toolbarGlyph(ToolGlyph::RuntimeUi));
                    runModeBtn->setIconSize(QSize(16, 16));
                    runModeBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                    runModeBtn->setAutoRaise(true);
                    runModeBtn->setToolTip(tr("切换到运行模式（自定义运行界面）"));
                    mainToolBar->addWidget(runModeBtn);
                }
            }

            // 模式切换逻辑 —— 保存到当前流程，同步执行器
            connect(m_flowModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int idx) {
                auto mode = static_cast<FlowMode>(
                    m_flowModeCombo->itemData(idx).toInt());
                m_executor->setFlowMode(mode);

                // 保存到当前流程并更新标签页标题
                int tabIdx = ui->flowTabs->currentIndex();
                if (tabIdx >= 0 && tabIdx < m_flowScenes.size()) {
                    m_flowModes[m_flowScenes[tabIdx]] = mode;
                    m_flowScenes[tabIdx]->setFlowMode(static_cast<int>(mode));   // 每流程模式写回场景，随方案保存
                    const QString suffix = flowModeSuffix(mode);   // F-3：后缀映射单一来源
                    // tab 标题用真实流程名（可能与 tab 序号不同，删除流程后更明显），避免显示与触发路由名不一致
                    const QString base = (m_executor && !m_executor->flowName().isEmpty())
                                            ? m_executor->flowName()
                                            : QStringLiteral("流程 %1").arg(tabIdx + 1);
                    ui->flowTabs->setTabText(tabIdx, base + suffix);
                }

                // 停止当前运行中的流程
                if (m_executor->getState() == ExecutionState::Running) {
                    m_executor->stopExecution();
                    m_executor->wait(500);
                }

                // 运行控制与模式选择解耦（对齐 VisionMaster）：切模式只决定"怎么跑"，不再自动开跑，
                // 一律由「开始执行」启动。此前连续/硬触发模式一切过去就自动 start：用户既找不到"启动"
                // 入口，也不理解为什么停止后起不来（开始按钮还被"仅软触发可用"规则禁用）。
                switch (mode) {
                case FlowMode::Continuous:
                    logMessage(QStringLiteral("已切换为连续模式：点「开始执行」开始循环运行，运行中可用「暂停」"));
                    break;
                case FlowMode::SoftwareTrigger:
                    logMessage(QStringLiteral("已切换为软触发模式：点「开始执行」运行一次流程"));
                    break;
                case FlowMode::HardwareTrigger:
                    logMessage(QStringLiteral("已切换为硬触发模式：点「开始执行」进入等待，相机每来一帧执行一次"));
                    break;
                }
                // 按钮态单一来源：开始/停止/单次执行可用性由 ExecutionStatusController 统一决定，
                // 不再在此手动写 setEnabled（消除「软触发才可开始」规则的第三份副本）。
                if (m_execStatus)
                    m_execStatus->updateButtons(m_executor ? m_executor->getState()
                                                          : ExecutionState::Stopped);
                updateEditLockForCurrentScene();
            });
        }

        // ---- 状态栏运行信息：运行状态 / 本次耗时 / 触发计数 ----
        // 状态机、三项常驻标签与「开始/停止/单次执行」按钮态集中到 ExecutionStatusController
        m_execStatus = new ExecutionStatusController(ui->statusBar, this);
        m_execStatus->setControls(ui->actionStartExecution, ui->actionStopExecution, m_singleShotBtn,
                                  m_pauseBtn);
        // 工具条图标化：暂停/继续两态用图标表达（文字仍由控制器同步，仅供 tooltip/无障碍）
        m_execStatus->setPauseIcons(toolbarGlyph(ToolGlyph::Pause), toolbarGlyph(ToolGlyph::Start));
        m_execStatus->setExecutorProvider([this]() { return m_executor; });

        // VM 5.2.5 响应配置：发送结果要在主窗口日志栏可见。此前 responseSent 全仓只有
        // emit 没有接收者，发送失败仅 VFP_DEBUG，现场完全看不出来（2026-11-10 审核）。
        // 只记失败，避免每轮成功响应刷屏；同样走 logMessage 以同步到运行界面「界面日志」。
        connect(CommResponseManager::instance(), &CommResponseManager::responseSent,
                this, [this](const QString &device, const QByteArray &, bool ok) {
                    if (!ok)
                        logMessage(QStringLiteral("通信响应发送失败：设备 %1 不存在或未连接")
                                       .arg(device));
                });

        // ---- P2：状态栏右侧「缩放百分比 + 版本号」（VM 4.4 状态栏口径）----
        {
            const QString statusLabelStyle =
                QStringLiteral("QLabel { color:#9fb3c8; font-size:12px; padding:0 8px; }");
            m_zoomStatusLabel = new QLabel(QStringLiteral("缩放 100%"), ui->statusBar);
            m_zoomStatusLabel->setStyleSheet(statusLabelStyle);
            m_zoomStatusLabel->setToolTip(tr("当前流程画布的缩放比例"));
            ui->statusBar->addPermanentWidget(m_zoomStatusLabel);

            // VM 显示「V4.4.0 Build20250516」；这里用构建注入的语义版本 + 构建日期
            QString buildDate = QStringLiteral(VFP_BUILD_DATE);
            buildDate.remove(QLatin1Char('-'));
            m_versionStatusLabel = new QLabel(
                QStringLiteral("V%1 Build%2")
                    .arg(QStringLiteral(VFP_VERSION_STRING), buildDate), ui->statusBar);
            m_versionStatusLabel->setStyleSheet(statusLabelStyle);
            m_versionStatusLabel->setToolTip(QStringLiteral(VFP_VERSION_DISPLAY));
            ui->statusBar->addPermanentWidget(m_versionStatusLabel);
        }

        // 空流程（启动/新建）时 VM 状态栏左侧常驻「等待添加模块」；这里用普通 addWidget
        //（非 permanent），操作提示仍可临时覆盖它，行为与 VM 一致。
        m_statusHintLabel = new QLabel(tr("等待添加模块"), ui->statusBar);
        m_statusHintLabel->setObjectName(QStringLiteral("statusHintLabel"));
        m_statusHintLabel->setStyleSheet(
            QStringLiteral("QLabel { color:#b8b8c0; font-size:12px; padding:0 8px; }"));
        ui->statusBar->addWidget(m_statusHintLabel);

    // 统一执行器信号连接（N3 富反馈统一）：首执行器不再直连（旧实现无门槛——后台跑首流程
        // 会污染状态栏/结果表），与 executorForScene 新建的执行器走**同一套**连接（见
        // connectExecutorSignals）：全局 UI 按 ex == m_executor 门槛，流程域反馈按执行器自身场景。
        connectExecutorSignals(m_executor);

        // 「视图 → 变量 / 性能统计」：动作在代码中创建（避免改动 .ui）；打开逻辑统一走
        // openAuxPanel，与「关闭时记住显隐、启动时恢复」共用同一条创建路径。
        if (ui->menuView) {
            ui->menuView->addSeparator();
            QAction *varAction = ui->menuView->addAction(tr("变量"));
            varAction->setObjectName(QStringLiteral("actionVariablePanel"));
            connect(varAction, &QAction::triggered, this,
                    [this]() { openAuxPanel(QStringLiteral("variable")); });

            // 性能面板（PerformancePanel）早已实现却没有任何入口（不可达代码），这里补上接线
            QAction *perfAction = ui->menuView->addAction(tr("性能统计"));
            perfAction->setObjectName(QStringLiteral("actionPerformancePanel"));
            connect(perfAction, &QAction::triggered, this,
                    [this]() { openAuxPanel(QStringLiteral("performance")); });

            // 统计报表（P1-11）：良率 / 按天趋势 / NG 按节点分布，可导出 CSV / HTML / 图表 PNG。
            // 做成对话框而非停靠面板：报表是"按需看一眼"的入口，无需参与面板显隐持久化。
            QAction *reportAction = ui->menuView->addAction(tr("统计报表"));
            reportAction->setObjectName(QStringLiteral("actionStatisticsReport"));
            connect(reportAction, &QAction::triggered, this, [this]() {
                StatisticsReportDialog dlg(this);
                dlg.exec();
            });
        }

        // 「视图 → 结果表」菜单项（动作定义在 .ui 中，接线方式与其它视图项一致）
        if (ui->actionResultTable) {
            connect(ui->actionResultTable, &QAction::triggered, this,
                    &MainWindow::onOpenResultTable);
        }


            
            

                

                

        
        // 初始化动作
        VFP_DEBUG << "Calling initActions";
        initActions();
        VFP_DEBUG << "initActions completed";
        
        // 初始化界面组件
        VFP_DEBUG << "Checking toolLibrary";
        if (ui->toolLibrary) {
            VFP_DEBUG << "Calling setupToolLibrary";
            setupToolLibrary();
            VFP_DEBUG << "setupToolLibrary completed";
        }
        
        VFP_DEBUG << "Calling setupFlowEditor";
        setupFlowEditor();
        VFP_DEBUG << "setupFlowEditor completed";
        
        VFP_DEBUG << "Calling setupRightPanel";
        setupRightPanel();
        VFP_DEBUG << "setupRightPanel completed";
        
        VFP_DEBUG << "Calling setupSchemeMenu";
        setupSchemeMenu();
        VFP_DEBUG << "setupSchemeMenu completed";

        // 菜单对话框路由面（词条切片 B 第二抽）：展示钩子即 showAuxDialog 的转发
        // （AuxPanelManager 同键复用/关闭即销毁/非模态语义不动，契约见该类头件与普查面段三）。
        m_menuRouter.setShow([this](const QString &key,
                                    const AuxDialogMenuRouter::DialogFactory &factory) {
            showAuxDialog(key, factory);
        });

        VFP_DEBUG << "Calling setupCommunicationMenu";
        setupCommunicationMenu();
        VFP_DEBUG << "setupCommunicationMenu completed";

        VFP_DEBUG << "Calling setupSystemMenu";
        setupSystemMenu();
        VFP_DEBUG << "setupSystemMenu completed";

        // 实装 4.4 菜单收敛：在此统一重排（此时各 setup 已把动作挂到旧菜单上）
        reorganizeMenuBar();
        
        // 初始语言与持久化设置保持一致（语言菜单切换后由 switchLanguage 重启生效）
        VFP_DEBUG << "Applying persisted language";
        m_currentLanguage = (I18n::currentLanguage() == I18n::Language::English)
                                ? Language::English
                                : Language::Chinese;
        updateLanguage();

        VFP_DEBUG << "createNewFlow";
        createNewFlow();
        VFP_DEBUG << "createNewFlow completed";

        // 自动保存/崩溃恢复：须在 createNewFlow 之后（首个流程已存在，序列化才有意义）。
        // 恢复询问延到窗口显示之后（见 showEvent → checkRecoveryOnStartup），
        // 否则构造期的模态框会挡住登录框。
        initCrashRecovery();
        initReportAutoExport();

        // P1-3：流程超时监控（UI 层计时，单次触发；执行不会因监控而阻塞）
        m_flowTimeoutTimer = new QTimer(this);
        m_flowTimeoutTimer->setSingleShot(true);
        connect(m_flowTimeoutTimer, &QTimer::timeout, this, &MainWindow::onFlowTimeout);
        
        // Phase 4: 登录 / 界面预览（--preview-ui）
        if (m_previewOnly) {
            // 预览模式跳过模态登录，但会话固定为**只读**：Viewer 角色（roleLevel 0）+ 写闸开启
            // ⇒ 方案编辑/保存、用户管理、通信配置、发起运行在权限层全部被拒；
            // 再显式禁用「打开方案 / 最近打开 / 开始 / 停止」，防止未登录读取本机方案。
            // 正常启动（无 --preview-ui）完全不经过本分支。
            VFP_DEBUG << "Preview UI mode: skip login, read-only session";
            m_loginAccepted = true;
            SessionManager *session = SessionManager::instance();
            session->login(tr("预览用户"), QStringLiteral("Viewer"));
            session->setWritesBlocked(true);
            setWindowTitle(tr("视觉方案工作台 - 界面预览（只读，未登录）"));
            updateUserButton();
            applyPermissionRestrictions();
            if (ui->actionOpenScheme)
                ui->actionOpenScheme->setEnabled(false);
            if (m_recentFiles && m_recentFiles->menu())
                m_recentFiles->menu()->setEnabled(false);
            if (ui->actionStartExecution)
                ui->actionStartExecution->setEnabled(false);
            if (ui->actionStopExecution)
                ui->actionStopExecution->setEnabled(false);
            ui->statusBar->showMessage(
                tr("界面预览模式：只读、未登录；保存 / 打开方案 / 运行均已禁用"), 0);
        } else {
            VFP_DEBUG << "Showing login dialog";
            UserLoginDialog loginDialog(this);
            if (loginDialog.exec() != QDialog::Accepted) {
                // A1-②：取消登录不再"默认以 Operator 身份进入"。
                // 旧实现等于发一张免鉴权的通行牌：能跑流程、能往库里写检测结果，
                // 但操作日志里全是 guest，事后查不到是谁干的。取消就是取消——
                // 不建会话、不进主界面，由 main() 看到 loginAccepted()==false 直接退出。
                VFP_DEBUG << "Login cancelled: no session created, main window stays hidden";
                return;
            }
            m_loginAccepted = true;
            const QString user = loginDialog.loggedInUser();
            const QString role = loginDialog.loggedInRole();
            SessionManager::instance()->login(user, role);
            VFP_DEBUG << "User logged in:" << user << "Role:" << role;
            setWindowTitle(QStringLiteral("视觉方案工作台 - %1 [%2]").arg(user, role));
            updateUserButton();
            applyPermissionRestrictions();
            // 脚本确认处于关闭态时的提示：**登录后**才写状态栏（构造期写会在窗口出现前过期）。
            // 写闸开启时这条会被合并进写闸的常驻提示，这里不再单独写（避免互相顶掉）。
            if (!ScriptSecurityPolicy::instance().requireConfirmation()
                && !SessionManager::instance()->writesBlocked()) {
                ui->statusBar->showMessage(
                    tr("注意：脚本执行前确认处于关闭状态（脚本将直接运行）——"
                       "可在「系统 → 脚本安全：执行前确认脚本」恢复"), 15000);
            }

            // 出厂默认口令仍在使用 ⇒ 上写操作闸并强制改密（取代历史的"弹一次告警就放行"）
            enforceFactoryPasswordPolicy();
        }
        VFP_DEBUG << (m_previewOnly ? "Preview UI ready" : "Login dialog completed");
        
        // 显示窗口
        setupSystemTray();
        VFP_DEBUG << "Showing MainWindow";
        show();
        VFP_DEBUG << "MainWindow shown";

        // 预览模式下的无人值守菜单快照：设置 VFP_MENU_SNAPSHOT_DIR 后，等首帧渲染完成
        // 抓取菜单栏/顶层菜单并退出，供 CI 或人工核对菜单结构（不影响交互预览）。
        if (m_previewOnly) {
            const QString snapshotDir = qEnvironmentVariable("VFP_MENU_SNAPSHOT_DIR");
            if (!snapshotDir.isEmpty()) {
                QTimer::singleShot(300, this, [this, snapshotDir]() {
                    captureMenuSnapshots(snapshotDir);
                    QCoreApplication::quit();
                });
            }
        }

        VFP_DEBUG << "MainWindow created successfully";
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in MainWindow constructor:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in MainWindow constructor";
    }
}

void MainWindow::captureMenuSnapshots(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists() && !QDir().mkpath(dirPath)) {
        VFP_DEBUG << "Menu snapshot: cannot create directory" << dirPath;
        return;
    }

    // 可选：按指定尺寸出图（如 1366x768），便于与 VM 同尺寸逐屏比对
    const QString sizeText = qEnvironmentVariable("VFP_UI_SNAPSHOT_SIZE");
    if (!sizeText.isEmpty()) {
        const QStringList wh = sizeText.split(QLatin1Char('x'), Qt::SkipEmptyParts);
        if (wh.size() == 2) {
            const int w = wh.at(0).toInt();
            const int h = wh.at(1).toInt();
            if (w > 200 && h > 200) {
                resize(w, h);
                QApplication::processEvents();
                // 停靠比例按最终截图尺寸再套一次：applyDefaultDockSizes 在首帧时算的是
                // 启动尺寸，先显示后 resize 会让「50% 图像区」停在旧窗口宽度对应的绝对像素。
                applyDefaultDockSizes();
                QApplication::processEvents();
            }
        }
    }

    // ① 文本结构：完整菜单树（含子菜单），便于逐项 diff，不受字体/分辨率影响
    QStringList lines;
    std::function<void(QMenu *, int)> dump = [&](QMenu *menu, int depth) {
        if (!menu)
            return;
        const QString pad(depth * 2, QLatin1Char(' '));
        for (QAction *action : menu->actions()) {
            if (action->isSeparator()) {
                lines << pad + QStringLiteral("---");
                continue;
            }
            QString text = action->text();
            text.remove(QLatin1Char('&'));
            lines << pad + text
                         + (action->isEnabled() ? QString() : QStringLiteral("  [disabled]"));
            if (QMenu *sub = action->menu())
                dump(sub, depth + 1);
        }
    };
    for (QAction *top : ui->menuBar->actions()) {
        if (!top)
            continue;
        QString text = top->text();
        text.remove(QLatin1Char('&'));
        lines << text;
        if (QMenu *menu = top->menu())
            dump(menu, 1);
    }
    QFile structureFile(dir.filePath(QStringLiteral("menu-structure.txt")));
    if (structureFile.open(QIODevice::WriteOnly | QIODevice::Text))
        structureFile.write(lines.join(QLatin1Char('\n')).toUtf8());

    // ② 主窗口整体 + 菜单栏截图（布局核对用；菜单弹出前抓，避免被弹出菜单遮住）
    grab().save(dir.filePath(QStringLiteral("window.png")));
    ui->menuBar->grab().save(dir.filePath(QStringLiteral("menubar.png")));

    // ③ 每个顶层菜单弹出后截图（屏幕外弹出避免占屏；抓完立即收起）
    for (QAction *top : ui->menuBar->actions()) {
        QMenu *menu = top ? top->menu() : nullptr;
        if (!menu)
            continue;
        menu->ensurePolished();
        menu->adjustSize();
        menu->popup(QPoint(-10000, -10000));
        QApplication::processEvents();
        QString name = top->text();
        name.remove(QLatin1Char('&'));
        name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),
                     QStringLiteral("_"));
        if (name.isEmpty())
            name = QStringLiteral("menu");
        menu->grab().save(dir.filePath(name + QStringLiteral(".png")));
        menu->hide();
        QApplication::processEvents();
    }
    VFP_DEBUG << "Menu snapshots saved to" << dirPath;
}

MainWindow::~MainWindow()
{
    // 停止并释放**全部**流程执行器（含当前激活的 m_executor）。
    // 顺序要求：必须在 qDeleteAll(m_flowScenes) 之前完成——历史缺陷：m_executor
    // 在场景删除之后才被 delete，且非激活执行器只等 1 秒就删；等待超时后
    // QThread 析构会 terminate() 强杀线程，而紧接的场景删除又会让仍在执行的
    // 线程访问已释放节点（use-after-free + 强杀线程叠加）。
    // 安全回收**全部**流程执行器（含当前激活的 m_executor）：先快照再逐个释放令牌，
    // 杜绝等待超时后 terminate() 硬杀线程（可能卡在 HALCON/SQLite/锁上，叠加场景析构=UAF）。
    // 词条切片 C P3b 期：执行器本体的所有权在令牌里 ⇒ 释放令牌即收尾（停止 ＋ 有界等待 ＋
    // 回收；超时绝不 terminate，转 finished 自删），与原回收件同口径、同顺序。
    QList<std::shared_ptr<FlowExecutorToken>> allTokens;
    for (auto it = m_flowExecutors.constBegin(); it != m_flowExecutors.constEnd(); ++it) {
        if (it.value())
            allTokens.append(it.value());
    }
    m_flowExecutors.clear();
    m_executor = nullptr;   // 已随列表摘除，置空避免悬垂
    for (auto &token : allTokens) {
        // 先等后台收尾（r19 P1-15）：令牌析构不再同步等，紧接着 qDeleteAll(m_flowScenes)
        // 会让仍在跑的线程访问已释放的节点。等不到才放弃（留痕）。
        if (token && !token->waitForFinish(15000))
            VFP_DEBUG << "FlowExecutor teardown timed out before scene deletion";
        token.reset();      // 最后一个令牌释放 ⇒ 令牌执行收尾
    }
    // 先断开场景 → 本窗的全部信号：下面先 delete ui、再 qDeleteAll(m_flowScenes)，
    // 若 nodeRemoved/nodeSelected 仍连着本窗，场景析构时会回调槽函数并访问已释放的 ui
    //（退出瞬间崩溃）。断开后场景析构只做数据回收。
    for (FlowScene *scene : m_flowScenes) {
        if (scene)
            QObject::disconnect(scene, nullptr, this, nullptr);
    }
    delete ui;
    qDeleteAll(m_flowScenes);
    m_flowScenes.clear();
    delete m_imageView;
}

// 多流程并发：获取场景专属执行器（不存在则创建并连接轻量信号）
FlowExecutor *MainWindow::executorForScene(FlowScene *scene)
{
    auto it = m_flowExecutors.constFind(scene);
    if (it != m_flowExecutors.cend())
        return it.value()->executor();
    // 首个流程复用初始执行器（其完整信号已在构造函数连接）
    std::shared_ptr<FlowExecutorToken> reuse = m_flowExecutors.value(nullptr);
    if (reuse && reuse->executor()->flowScene() == nullptr) {
        m_flowExecutors.remove(nullptr);
        m_flowExecutors.insert(scene, reuse);
        return reuse->executor();
    }
    // 词条切片 C P3b 期：新建即建令牌（令牌接管本体的所有权，删除权归令牌）；
    // P3c 期起经 create 建——它同时把弱引用挂到本体（节点归属与触发路由据此取用）。
    std::shared_ptr<FlowExecutorToken> token = FlowExecutorToken::create(new FlowExecutor());
    FlowExecutor *ex = token->executor();
    connectExecutorSignals(ex);
    m_flowExecutors.insert(scene, token);
    return ex;
}

void MainWindow::releaseExecutorToken(std::shared_ptr<FlowExecutorToken> &token)
{
    if (!token)
        return;
    // 收尾的唯一实现住在令牌件里（等待成功即回收本体；等待超时绝不强杀，转 finished 自删）——
    // 界面侧只负责「把令牌放掉」，不再自己 delete 执行器 ⇒ 本体的删除只有一处，两路口径不分叉。
    token.reset();
}

// 多流程并发：**唯一**的执行器信号连接点（N3 富反馈统一）。首执行器（构造函数）与
// executorForScene 新建执行器都走这里，规则完全一致：
//  · 全局 UI（状态栏 / 结果表 / 变量面板 / 最近变量缓存 / 图像显示 / 运行界面 / 参数面板）
//    只接受"当前激活执行器"（ex == m_executor）的推送——否则后台跑首流程会污染状态栏与结果表；
//  · 流程域反馈（日志 + 节点着色）不设门槛，按执行器自身场景（ex->flowScene()）更新，
//    保证后台流程也有逐节点反馈（旧实现后台执行器完全没有逐节点反馈）。
void MainWindow::connectExecutorSignals(FlowExecutor *ex)
{
    if (!ex) return;

    // ── 全局 UI：运行状态 → 状态栏（状态/耗时/触发计数） ──
    connect(ex, &FlowExecutor::executionStarted, this, [this, ex]() {
        if (ex == m_executor) onExecutionStarted();
    });
    // VM 5.2.5 流程控制响应：忙碌=开始、空闲=结束（不按当前激活流程过滤：后台流程状态同样要上报）。
    // 旧实现只在 executionFinished 发 IDLE，手动停止 / 运行错误 / 终局撤除时 PLC 会
    // 一直停在 BUSY（2026-11-10 审核）。统一用 reportIdle 覆盖全部终态，
    // 重复终态由 CommResponseManager 的边沿去重吞掉。
    auto reportIdle = [ex]() {
        FlowScene *s = ex->flowScene();
        CommResponseManager::instance()->handleFlowState(
            s ? s->flowName() : QString(), false);
    };
    connect(ex, &FlowExecutor::executionStarted, this, [ex]() {
        FlowScene *s = ex->flowScene();
        CommResponseManager::instance()->handleFlowState(s ? s->flowName() : QString(), true);
    });
    connect(ex, &FlowExecutor::executionFinished, this, [reportIdle]() {
        reportIdle();
    });
    connect(ex, &FlowExecutor::executionStopped, this, [this, ex, reportIdle]() {
        if (ex == m_executor) onExecutionStopped();
        reportIdle();
    });
    connect(ex, &FlowExecutor::executionFinished, this, [this, ex]() {
        if (ex == m_executor) onExecutionFinished();
    });
    // 良率目标报警：**不按"当前激活流程"过滤**——后台流程的轮次同样要参与判定，
    // 否则多流程并发时报警会漏（哪条流程超差都得报）。节流在 checkYieldTarget 内部。
    connect(ex, &FlowExecutor::executionFinished, this, [this, ex]() {
        checkYieldTarget(ex);
    });

    // ── 图编辑锁（S1 / Phase B 小步）：按执行器自身场景更新，不按"当前激活流程"——
    //    后台流程暂停停稳/恢复时若漏更新，就会出现"后台正在跑却可编辑"的漏洞。
    //    · executionParked：worker 真停进等待点（当前节点已跑完）→ 解锁，可安全改图
    //    · executionResumed：恢复执行 → 立即加锁（否则暂停中改完图点继续，运行中仍是可编辑）
    connect(ex, &FlowExecutor::executionParked, this, [this, ex]() {
        if (FlowScene *s = ex->flowScene())
            s->setEditLocked(SessionManager::instance()->canvasEditShouldBeLocked(
                !ex->allowsGraphEditing()));
        // 停稳暂停已允许改图（2026-10-29 r16 P2-13）：菜单要跟着解锁
        applyPermissionRestrictions();
    });
    connect(ex, &FlowExecutor::executionResumed, this, [this, ex]() {
        if (FlowScene *s = ex->flowScene())
            s->setEditLocked(SessionManager::instance()->canvasEditShouldBeLocked(
                !ex->allowsGraphEditing()));
        // 恢复执行立刻再锁上
        applyPermissionRestrictions();
    });
    // 运行控制的状态反馈：暂停请求已受理 / 真停稳 / 已继续（ex == m_executor 门槛，避免后台流程
    // 污染当前状态栏与按钮态）
    connect(ex, &FlowExecutor::executionPaused, this, [this, ex]() {
        if (ex == m_executor && m_execStatus) m_execStatus->onPaused();
    });
    connect(ex, &FlowExecutor::executionParked, this, [this, ex]() {
        if (ex == m_executor && m_execStatus) m_execStatus->onParked();
    });
    connect(ex, &FlowExecutor::executionResumed, this, [this, ex]() {
        if (ex == m_executor && m_execStatus) m_execStatus->onResumed();
    });
    connect(ex, &FlowExecutor::executionError, this, [this, ex, reportIdle](const QString &err) {
        if (ex == m_executor) onExecutionError(err);
        else logMessage(QStringLiteral("流程错误: %1").arg(err));
        reportIdle();   // 错误也是终态：不能让 PLC 卡在 BUSY
    });
    // P3d（终局撤除）：令牌收尾超时 ⇒ 可观测终态。专用信号不走 executionStopped——
    // 后者会解锁图，而此刻被放弃的工作线程可能仍在跑；这里只更新状态栏与按钮态。
    // 日志直接记 payload（原因串自带语境：令牌收尾超时与终态处置），避免与控制器
    // 的状态栏提示句形成同句双份。
    connect(ex, &FlowExecutor::executionAbandoned, this,
            [this, ex, reportIdle](const QString &why) {
        if (ex == m_executor && m_execStatus) m_execStatus->onAbandoned(why);
        logMessage(why);
        reportIdle();   // 终局撤除同样要回到 IDLE
    });

    // ── 全局 UI：运行模式变更 → 刷新所有场景 MVS 节点的像素格式可用性 ──
    // （⑤a：旧实现在构造函数只内联连首执行器，新/后台流程的模式切换不刷新；
    // 统一在此连接，按 ex == m_executor 门槛，保证仅当前激活流程刷新 UI。）
    connect(ex, &FlowExecutor::flowModeChanged, this, [this, ex](int mode) {
        Q_UNUSED(mode)
        if (ex != m_executor)
            return;
        for (FlowScene *scene : m_flowScenes) {
            for (NodeBase *node : scene->nodes()) {
                if (auto *mvs = qobject_cast<MvsImageSourceNode *>(node))
                    mvs->refreshPixelFormatEnabled();
            }
        }
    });

    // ── 全局 UI：跳过三态（未激活分支 / 循环体调度的节点标"跳过"） ──
    connect(ex, &FlowExecutor::nodeSkipped, this,
            [this, ex](NodeBase *node, const QString &reason) {
        Q_UNUSED(reason)
        if (ex != m_executor || !isNodeLive(node, ex->flowScene()))
            return;
        // 立刻重绘（2026-10-28 复核 P3）：第三态只置了节点标志，画布要等下次重绘才变色
        if (FlowScene *scene = currentFlowScene())
            scene->update();
        if (!node)
            return;
        for (ResultTablePanel *rt : resultPanels())
            rt->setModuleSkipped(node->moduleId(), node->fullName());
    });

    // ── 全局 UI：结果数据表 / 变量面板（节点执行后把该模块输出推到面板） ──
    connect(ex, &FlowExecutor::nodeOutputsUpdated, this,
            [this, ex](NodeBase *node, bool ok, qint64 elapsedMs, const QVariantMap &vars) {
        if (ex != m_executor || !isNodeLive(node, ex->flowScene()))
            return;
        for (ResultTablePanel *rt : resultPanels()) {
            // VM「模块结果」口径：算子自己声明结果字段（顺序/名称/单位/精度），
            // 未声明的配置参数不再混进结果表；未对齐的算子返回空表，走旧平铺行为。
            rt->setModuleResult(node->moduleId(), node->fullName(), ok, elapsedMs, vars,
                                node->resultDisplayFields());
        }
        // 供「变量引用」菜单构造引用列表（UI 线程缓存，不读执行线程内部状态）
        // U-18：判红轮的快照现在带"原因"字段，那是给人看的出口内容（结果面板／CSV／报告），
        // 不是可引用变量。这张缓存喂「变量引用」菜单（buildVariableReferences）与发送事件
        // payload（fireEnabledSendEvents）两个**对外报文面** ⇒ 维持原口径：失败轮插空表。
        // "要不要把原因做成可引用变量／进上报报文"属产品决策，本轮不顺手动。
        m_lastModuleVars.insert(node->moduleId(), ok ? vars : QVariantMap());
        // P2-4：缓存模块耗时，并刷新画布角标（选中该模块时立即更新）
        m_nodeElapsed.insert(node, elapsedMs);
        if (m_selectedNode == node)
            updateCanvasSelectionElapsed(node);
        // 变量面板只列可引用的值：失败节点本轮没有可引用输出
        if (auto *vp = m_auxPanels ? m_auxPanels->variablePanel() : nullptr) {
            if (ok)
                vp->setModuleVars(node->moduleId(), node->fullName(), vars);
        }
    });

    // ── 全局 UI：末端图像 → 运行界面 + 图像显示 ──
    connect(ex, &FlowExecutor::imageReady, this,
            [this, ex](NodeBase *node, const HalconCpp::HImage &image) {
        if (ex != m_executor || !isNodeLive(node, ex->flowScene()))
            return;
        // 运行界面图像控件转发（不受用户显示选择影响）
        if (m_runtimeView)
            RuntimeInterfaceView::broadcastImage(node->fullName(), image);
        // 如果用户已通过下拉框或画布选择了一个算子，imageReady 不覆盖
        NodeBase *target = resolveDisplayNode();
        if (target != node && target != nullptr) {
            VFP_DEBUG << "用户已选择显示:" << target->fullName() << "，imageReady不覆盖";
            return;
        }
        // 兜底：自动显示最后一个节点的图像（信号负载的图像 + 该节点的叠加图元）
        if (m_imageDisplay)
            m_imageDisplay->showImage(image, node->fullName(), node);
    });

    // ── 全局 UI：任意节点图像 → 运行界面按节点名推送（绑定中间节点控件可实时显示） ──
    connect(ex, &FlowExecutor::imageAvailable, this,
            [this, ex](NodeBase *node, const HalconCpp::HImage &image) {
        if (ex != m_executor || !isNodeLive(node, ex->flowScene()))
            return;
        if (m_runtimeView)
            RuntimeInterfaceView::broadcastImage(node->fullName(), image);
    });

    // ── 逐节点反馈 ──
    connect(ex, &FlowExecutor::nodeExecuted, this, [this, ex](NodeBase *node, bool success) {
        // 流程域：日志（后台流程同样输出）
        logMessage(QStringLiteral("节点执行结果: %1").arg(success ? QStringLiteral("成功")
                                                                 : QStringLiteral("失败")));
        if (!isNodeLive(node, ex->flowScene()))
            return;
        // 流程域：节点着色按执行器自身场景（旧实现只看当前标签页场景 → 后台流程无反馈/串场景）
        if (FlowScene *scene = ex->flowScene()) {
            if (NodeGraphicsItem *item = scene->getGraphicsItemForNode(node))
                item->update();
        }

        // 全局 UI 部分只在"失败为假 且 当前激活流程"时更新
        if (!success || ex != m_executor)
            return;

        // 参数面板：显示最新输出参数（选中节点）
        if (m_selectedNode) {
            QWidget *parameterPanel = ui->paramDockContents;
            if (parameterPanel) {
                // 查找参数面板中的所有子控件（输出页 + 输入页随运行刷新）
                const QList<QWidget*> childWidgets = parameterPanel->findChildren<QWidget*>();
                for (QWidget *childWidget : childWidgets) {
                    QTabWidget *tabWidget = qobject_cast<QTabWidget*>(childWidget);
                    if (!tabWidget)
                        continue;
                    if (QWidget *outputTab = tabWidget->findChild<QWidget*>("outputTab"))
                        m_selectedNode->updateParamPanel(outputTab);
                    // 输入面板随运行刷新（图像源信息等）
                    if (QWidget *inputTab = tabWidget->findChild<QWidget*>("inputTab"))
                        m_selectedNode->updateParamPanel(inputTab);
                }
            }
        }

        // 图像显示：核心规则 = 下拉框选择 > 画布选中 > 当前执行节点
        NodeBase *targetDisplayNode = resolveDisplayNode(node);
        if (targetDisplayNode && m_imageView) {
            QSharedPointer<DataObject> outputData = targetDisplayNode->getOutputData(0);
            if (outputData) {
                const HImage image = outputData->getHImage();
                if (image.IsInitialized()) {
                    m_imageView->requestImage(image, targetDisplayNode->fullName());
                    if (m_imageSourceLabel) {
                        m_imageSourceLabel->setText(
                            QStringLiteral("图像来源: %1").arg(targetDisplayNode->fullName()));
                    }
                }
            }
        }

        // 运行界面：转发节点输出值（数值显示 / 状态灯 / IO状态）
        if (m_runtimeView) {
            // 全端口输出映射：端口0 走 updateNodeOutput（数值/状态灯）；
            // 全部端口按名走 updateNodePortMap（IO状态控件消费 成功/值/错误）
            QVariantMap portMap;
            // B1 统一输出：一个端口 + 具名子结果（值/成功/错误、匹配结果…）。
            // 按名取值而不是按端口号，IO 状态灯等按名绑定的控件才不受聚合影响。
            const auto named = node->namedOutputs();
            for (int pi = 0; pi < named.size(); ++pi) {
                QSharedPointer<DataObject> out = named.at(pi).second;
                if (!out) continue;
                QVariant v;
                switch (out->getType()) {
                case DataObject::DataType::Number:
                case DataObject::DataType::String:
                case DataObject::DataType::Bool:
                case DataObject::DataType::Array:
                    v = out->getData();
                    break;
                case DataObject::DataType::Measure: {
                    const MeasureResult mr = out->getMeasureResult();
                    if (mr.valid) v = mr.value;
                    break;
                }
                case DataObject::DataType::Point: {
                    const QPointF p = out->getPoint();
                    v = QStringLiteral("(%1, %2)").arg(p.x(), 0, 'f', 3).arg(p.y(), 0, 'f', 3);
                    break;
                }
                default:
                    break;
                }
                if (v.isValid()) {
                    portMap.insert(named.at(pi).first, v);
                    if (pi == 0)
                        RuntimeInterfaceView::broadcastNodeOutput(node->fullName(), v);
                }
            }
            if (!portMap.isEmpty())
                RuntimeInterfaceView::broadcastNodePortMap(node->fullName(), portMap);
        }
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 未保存确认必须最先做：取消关闭时不得已经产生副作用（清恢复现场/停相机/落面板配置）
    if (!confirmCloseWithUnsavedChanges()) {
        VFP_DEBUG << "closeEvent: 用户在\"未保存修改\"确认中选择取消，本次关闭被忽略";
        event->ignore();
        return;
    }

    // 后台运行（VM 4.4 口径）：有流程在跑时询问"是否保持后台运行?"
    // 托盘不可用时不提供该选项（隐藏后无法恢复主界面，宁可正常停流程退出）
    if (!m_forceQuit && m_trayIcon && anyFlowRunning()) {
        QMessageBox box(QMessageBox::Question, tr("后台运行"),
                        tr("检测到流程正在运行。是否保持后台运行？"), QMessageBox::NoButton, this);
        QPushButton *keepBtn = box.addButton(tr("保持后台运行"), QMessageBox::AcceptRole);
        QPushButton *quitBtn = box.addButton(tr("退出软件，停止运行"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(keepBtn);
        box.exec();
        if (box.clickedButton() == keepBtn) {
            minimizeToTray();
            event->ignore();
            return;
        }
        if (box.clickedButton() != quitBtn) {
            event->ignore();
            return;
        }
    }

    // 关闭路径留痕：现场反馈过"关了软件但进程还在"，日志里没有本条即说明根本没走到正常关闭
    // （例如进程停在登录框/无窗口状态），据此可快速区分"没关掉"与"关掉后没退干净"。
    VFP_DEBUG << "closeEvent: 关闭主窗口，保存面板显隐并清理相机…";
    // 记住辅助面板显隐，下次启动时恢复（避免每次重启都要重新打开结果表/变量面板）
    saveAuxPanelVisibility();
    // 记住停靠布局：用户拖过的左右占比/面板位置下次启动原样恢复
    QSettings().setValue(QStringLiteral("ui/mainWindowState2"), saveState());
    cleanupCameras();
    LargeFileImageStore::instance()->flush();   // 采集策略：退出前把未满一批落盘
    if (m_trayIcon)
        m_trayIcon->hide();   // 正常退出时摘掉托盘图标，避免残留
    QMainWindow::closeEvent(event);
}

bool MainWindow::anyFlowRunning() const
{
    QSet<FlowExecutor *> seen;
    for (const auto &token : m_flowExecutors) {
        FlowExecutor *ex = token ? token->executor() : nullptr;
        if (!ex || seen.contains(ex))
            continue;
        seen.insert(ex);
        if (ex->isRunning())
            return true;
    }
    return m_executor && m_executor->isRunning() && !seen.contains(m_executor);
}

void MainWindow::setupSystemTray()
{
    if (m_trayIcon || !QSystemTrayIcon::isSystemTrayAvailable())
        return;
    // 托盘图标不能为空：Windows 下空图标会占位不显示，用户找不到恢复入口
    QIcon icon = windowIcon().isNull() ? QApplication::windowIcon() : windowIcon();
    if (icon.isNull())
        icon = style()->standardIcon(QStyle::SP_ComputerIcon);
    m_trayIcon = new QSystemTrayIcon(icon, this);
    m_trayIcon->setToolTip(tr("视觉方案工作台（后台运行）"));
    QMenu *menu = new QMenu(this);
    QAction *showAct = menu->addAction(tr("显示主界面"));
    QAction *quitAct = menu->addAction(tr("退出软件（停止运行）"));
    m_trayIcon->setContextMenu(menu);
    connect(showAct, &QAction::triggered, this, &MainWindow::restoreFromTray);
    connect(quitAct, &QAction::triggered, this, [this]() {
        m_forceQuit = true;
        restoreFromTray();
        // 先让托盘菜单事件退出，再走正常关闭路径（未保存确认/停流程/清相机）
        QTimer::singleShot(0, this, [this]() { close(); });
    });
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger
                    || reason == QSystemTrayIcon::DoubleClick) {
                    restoreFromTray();
                }
            });
}

void MainWindow::minimizeToTray()
{
    if (!m_trayIcon) {
        logMessage(tr("系统托盘不可用，无法进入后台运行"));
        return;
    }
    m_trayIcon->show();
    hide();
    m_trayIcon->showMessage(tr("后台运行"),
                            tr("流程继续执行，双击托盘图标可恢复主界面。"),
                            QSystemTrayIcon::Information, 3000);
    logMessage(tr("已进入后台运行：流程继续执行"));
}

void MainWindow::restoreFromTray()
{
    showNormal();
    raise();
    activateWindow();
    if (m_trayIcon)
        m_trayIcon->hide();
}

void MainWindow::cleanupCameras()
{
    VFP_DEBUG << "正在关闭所有相机...";

    // 1. 停止**全部**流程执行（释放相机采集线程）
    //    原实现只停 m_executor：多流程并发时其它 FlowExecutor 仍在
    //    MvsImageSourceNode::grabImage 的 MV_CC_GetOneFrameTimeout 中阻塞取流，
    //    随后第 3 步直接 DestroyHandle 会造成 use-after-free。
    QList<FlowExecutor *> executors;
    for (const auto &token : m_flowExecutors) {
        FlowExecutor *ex = token ? token->executor() : nullptr;
        if (ex && !executors.contains(ex))
            executors.append(ex);
    }
    if (m_executor && !executors.contains(m_executor))
        executors.append(m_executor);

    for (FlowExecutor *ex : executors)
        ex->stopExecution();
    QList<FlowExecutor *> stuck;
    // 共享截止时间（r19 P2）：原先是每个执行器各等 3000ms，N 条流程关窗最坏 3N 秒。
    // 现在总共最多 3 秒——先全部发过停止，再按剩余时间等。
    QElapsedTimer closeWait;
    closeWait.start();
    for (FlowExecutor *ex : executors) {
        if (!ex->isRunning())
            continue;
        const int remain = 3000 - int(closeWait.elapsed());
        if (remain <= 0 || !ex->wait(remain))
            stuck.append(ex);
    }
    if (!stuck.isEmpty()) {
        // 有执行线程没停下来：此时调 closeCamera / DestroyHandle 等于在工作线程
        // 仍可能持有 SDK 句柄时拆句柄——正是本函数开头注释警告过的 use-after-free
        // 同类（MvsImageSourceNode::grabImage 里的 MV_CC_GetOneFrameTimeout 可能
        // 在 SDK 内部卡住超过 3 秒）。宁可不关相机（进程退出时由 OS/驱动回收），
        // 也不在握有活线程时拆句柄。与 onCloseFlowTab 的"超时即中止、不动现场"同口径。
        logMessage(tr("有 %1 个流程执行线程未能在 3 秒内退出，已跳过相机关闭"
                      "（请勿在流程运行中直接关窗；可在停止执行后再关闭）")
                       .arg(stuck.size()));
        return;
    }

    // 2. 关闭所有 MVS 图像源节点的本地相机（按顺序清理每个 flow scene）
    for (FlowScene *scene : m_flowScenes) {
        if (!scene) continue;
        for (NodeBase *node : scene->nodes()) {
            auto *mvs = qobject_cast<MvsImageSourceNode*>(node);
            if (mvs) {
                // 标记该节点的全局相机为未使用（由节点自行 closeCamera）
                mvs->closeCamera();
            }
        }
    }

    // 3. 关闭所有 GlobalCameraManager 中打开的全局相机
    GlobalCameraManager *gcm = GlobalCameraManager::instance();
    // 获取所有已注册的全局相机名称
    QMap<QString, GlobalCameraManager::Camera> allCameras = gcm->cameras();
    for (auto it = allCameras.begin(); it != allCameras.end(); ++it) {
        const QString &cameraName = it.key();
        if (gcm->isCameraOpen(cameraName)) {
            // 关机清理：此时上面的 MVS 节点已各自 closeCamera，消费者账可能还没随析构清掉，
            // 所以这里显式 force（P2-4：默认路径会被消费者闸拒，而那会让关窗时相机全不关）
            gcm->closeCamera(cameraName, /*force*/ true);
        }
    }

    VFP_DEBUG << "所有相机已关闭";
}

void MainWindow::initActions()
{
    // 创建新方案
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::onNewProject);

    // 快捷键体系（对标 VM 4.4 常用操作）
    ui->actionNew->setShortcut(QKeySequence::New);                    // Ctrl+N
    ui->actionOpenScheme->setShortcut(QKeySequence::Open);            // Ctrl+O
    ui->actionSaveScheme->setShortcut(QKeySequence::Save);            // Ctrl+S
    ui->actionStartExecution->setShortcut(QKeySequence(Qt::Key_F5));  // F5 运行
    ui->actionStopExecution->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5)); // Shift+F5 停止

    // 实装 4.4：F6 连续运行、Esc 停止（欢迎页此前已这样写，但快捷键没绑定）
    {
        auto *continuousShortcut = new QShortcut(QKeySequence(Qt::Key_F6), this);
        connect(continuousShortcut, &QShortcut::activated, this, [this]() {
            if (m_flowModeCombo) {
                const int idx = m_flowModeCombo->findData(
                    static_cast<int>(FlowMode::Continuous));
                if (idx >= 0)
                    m_flowModeCombo->setCurrentIndex(idx);
            }
            onStartExecution();
        });
        auto *stopShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(stopShortcut, &QShortcut::activated, this, [this]() {
            if (!m_executor)
                return;
            const ExecutionState state = m_executor->getState();
            if (state == ExecutionState::Running || state == ExecutionState::Paused)
                onStopExecution();
        });
    }

    // 添加/删除流程：Ctrl+T 快捷键 + 流程标签栏右上角「＋」按钮。
    // 此前入口只有「编辑」菜单里的英文项 "Add Flow"，中文界面下很难发现。
    ui->actionAddFlow->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    ui->actionDeleteFlow->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    // 流程栏右侧：运行 / 循环运行 / 新建流程（按 VM 流程栏同区控制做成图标，2026-11-07）
    // 运行按钮直接触发源动作，可用态跟随 actionStartExecution（权限/写闸/运行态规则不在此复制）。
    {
        auto *flowCorner = new QWidget(ui->flowTabs);
        flowCorner->setObjectName(QStringLiteral("flowBarCorner"));
        auto *cornerLay = new QHBoxLayout(flowCorner);
        cornerLay->setContentsMargins(0, 0, 4, 0);
        cornerLay->setSpacing(1);

        const QColor flowInk(0xb8, 0xb8, 0xb8);
        auto *runBtn = new QToolButton(flowCorner);
        runBtn->setObjectName(QStringLiteral("flowRunBtn"));
        runBtn->setIcon(toolbarGlyph(ToolGlyph::Start, flowInk, 1.6));
        runBtn->setToolTip(tr("运行当前流程（F5）"));
        auto *loopBtn = new QToolButton(flowCorner);
        loopBtn->setObjectName(QStringLiteral("flowLoopRunBtn"));
        loopBtn->setIcon(toolbarGlyph(ToolGlyph::LoopRun, flowInk, 1.6));
        loopBtn->setToolTip(tr("连续运行当前流程（F6）"));
        auto *addFlowBtn = new QToolButton(flowCorner);
        addFlowBtn->setObjectName(QStringLiteral("flowAddBtn"));
        addFlowBtn->setText(QStringLiteral("＋"));
        addFlowBtn->setToolTip(QStringLiteral("添加新流程（Ctrl+T）"));
        QFont plusFont = addFlowBtn->font();
        plusFont.setPointSizeF(plusFont.pointSizeF() + 3);
        plusFont.setBold(true);
        addFlowBtn->setFont(plusFont);

        for (QToolButton *btn : {runBtn, loopBtn}) {
            btn->setAutoRaise(true);
            btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            btn->setIconSize(QSize(16, 16));
            btn->setFixedSize(24, 24);
        }
        addFlowBtn->setAutoRaise(true);
        addFlowBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        addFlowBtn->setFixedSize(24, 24);

        connect(runBtn, &QToolButton::clicked, ui->actionStartExecution, &QAction::trigger);
        connect(loopBtn, &QToolButton::clicked, this, [this]() {
            // 与 F6 同义：先切连续模式，再开始执行
            if (m_flowModeCombo) {
                const int idx = m_flowModeCombo->findData(static_cast<int>(FlowMode::Continuous));
                if (idx >= 0)
                    m_flowModeCombo->setCurrentIndex(idx);
            }
            onStartExecution();
        });
        connect(addFlowBtn, &QToolButton::clicked, this, &MainWindow::onAddFlowTab);

        const auto syncRunButtons = [this, runBtn, loopBtn]() {
            const bool enabled = ui->actionStartExecution->isEnabled();
            runBtn->setEnabled(enabled);
            loopBtn->setEnabled(enabled);
        };
        connect(ui->actionStartExecution, &QAction::changed, flowCorner, syncRunButtons);
        syncRunButtons();

        cornerLay->addWidget(runBtn);
        cornerLay->addWidget(loopBtn);
        cornerLay->addWidget(addFlowBtn);
        ui->flowTabs->setCornerWidget(flowCorner, Qt::TopRightCorner);
    }

    // 撤销/重做（Ctrl+Z / Ctrl+Y，作用于当前流程标签页）
    auto currentScene = [this]() -> FlowScene * {
        const int idx = ui->flowTabs->currentIndex();
        if (idx >= 0 && idx < m_flowScenes.size()) return m_flowScenes[idx];
        return nullptr;
    };
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, [currentScene]() {
        if (FlowScene *s = currentScene()) s->undo();
    });
    auto *redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(redoShortcut, &QShortcut::activated, this, [currentScene]() {
        if (FlowScene *s = currentScene()) s->redo();
    });
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(deleteShortcut, &QShortcut::activated, this, [currentScene]() {
        // 焦点在可编辑控件里时，Delete 是文本编辑操作，不能拿去删节点
        // （2026-11-08 复核）：QShortcut 默认 WindowShortcut 上下文，会劫持
        // 参数面板 QLineEdit / 结果表 QTextEdit / 数值框里的删除键——用户在
        // 输入框里按 Delete，画布上选中的节点就被删了。
        if (isTextEditingFocus())
            return;
        if (FlowScene *s = currentScene()) s->deleteSelectedItems();
    });

    // 算子分组（FR1.9）：Ctrl+G 创建 / Ctrl+Shift+G 解散。
    // 入口同时进「编辑」菜单：只在右键菜单里放，会重蹈"Add Flow 藏在英文菜单里没人发现"的覆辙。
    m_actionCreateGroup = new QAction(tr("创建分组"), this);
    m_actionCreateGroup->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    m_actionCreateGroup->setStatusTip(tr("把当前选中的算子框成一组（纯视觉容器，不影响执行）"));
    m_actionDissolveGroup = new QAction(tr("解散分组"), this);
    m_actionDissolveGroup->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+G")));
    m_actionDissolveGroup->setStatusTip(tr("删除选中的分组框，组内算子保留"));
    // 折叠/展开（FR1.9 的"Group 折叠"）：纯显示动作，不改变执行；双击分组标题栏是同一效果。
    // 不给快捷键：折叠没有公认的通用键位，硬塞一个反而抢键。
    m_actionToggleGroup = new QAction(tr("折叠/展开分组"), this);
    m_actionToggleGroup->setStatusTip(tr("把选中的分组折叠成一条标题栏（纯显示，不影响执行）"));

    // 子图复用（FR15.10 的设计期半步）：复制/粘贴选中子图 + 片段文件导入导出。
    // 快捷键用 WidgetShortcut（**只在主窗口自身有焦点时**生效）：Ctrl+C/V 不抢文本框的复制粘贴
    // ——画布上的 Ctrl+C/V 由 FlowScene 的按键处理发出请求（见 hookFlowScene）。
    m_actionCopySnippet = new QAction(tr("复制所选算子"), this);
    m_actionCopySnippet->setShortcut(QKeySequence::Copy);
    m_actionCopySnippet->setShortcutContext(Qt::WidgetShortcut);
    m_actionPasteSnippet = new QAction(tr("粘贴"), this);
    m_actionPasteSnippet->setShortcut(QKeySequence::Paste);
    m_actionPasteSnippet->setShortcutContext(Qt::WidgetShortcut);
    m_actionExportSnippet = new QAction(tr("导出片段…"), this);
    m_actionExportSnippet->setStatusTip(tr("把选中的算子（含内部连线）导出为片段文件，供别的方案插入"));
    m_actionImportSnippet = new QAction(tr("导入片段…"), this);
    m_actionImportSnippet->setStatusTip(tr("把片段文件插入到当前流程（放在当前视图中心）"));
    if (ui->menuEdit) {
        QAction *beforeSnippet = ui->actionDeleteFlow;
        ui->menuEdit->insertAction(beforeSnippet, m_actionCopySnippet);
        ui->menuEdit->insertAction(beforeSnippet, m_actionPasteSnippet);
        ui->menuEdit->insertAction(beforeSnippet, m_actionExportSnippet);
        ui->menuEdit->insertAction(beforeSnippet, m_actionImportSnippet);
        ui->menuEdit->insertSeparator(beforeSnippet);
    }
    connect(m_actionCopySnippet, &QAction::triggered, this,
            [this]() { copySelectionToClipboard(currentFlowScene()); });
    connect(m_actionPasteSnippet, &QAction::triggered, this,
            [this]() { pasteSnippetIntoScene(currentFlowScene()); });
    connect(m_actionExportSnippet, &QAction::triggered, this, &MainWindow::onExportSnippet);
    connect(m_actionImportSnippet, &QAction::triggered, this, &MainWindow::onImportSnippet);

    // 加密 / 只读方案导出（G-P1-10）：文件菜单下追加入口（此段在 m_recentFiles 之前执行，
    // 故动作落在"最近文件"子菜单之前，顺序合理）
    m_actionExportEncrypted = new QAction(tr("导出加密/只读方案…"), this);
    m_actionExportEncrypted->setStatusTip(QStringLiteral(
        "把当前方案加密并可选只读打包，分发后现场无法反编译/覆盖保存"));
    if (ui->menuFile)
        ui->menuFile->addAction(m_actionExportEncrypted);
    connect(m_actionExportEncrypted, &QAction::triggered,
            this, &MainWindow::onExportEncryptedProject);
    if (ui->menuEdit) {
        QAction *before = ui->actionDeleteFlow;
        ui->menuEdit->insertAction(before, m_actionCreateGroup);
        ui->menuEdit->insertAction(before, m_actionDissolveGroup);
        ui->menuEdit->insertAction(before, m_actionToggleGroup);
        ui->menuEdit->insertSeparator(before);
        // FR15.10 运行期子流程：定义/删除入口（与分组动作相邻，语义同属"画布组织"）
        m_actionDefineSubFlow = new QAction(tr("定义为子流程…"), this);
        m_actionDefineSubFlow->setStatusTip(tr(
            "把选中的算子（≥2，单进单出、不与成员外连线）定义为命名子流程，"
            "供「子流程」算子按名调用；一处修改、所有引用同步生效"));
        m_actionRemoveSubFlow = new QAction(tr("删除子流程定义…"), this);
        m_actionRemoveSubFlow->setStatusTip(tr(
            "删除一个子流程定义；画布算子原样保留，引用它的「子流程」算子执行时将报「未定义」"));
        ui->menuEdit->insertAction(before, m_actionDefineSubFlow);
        ui->menuEdit->insertAction(before, m_actionRemoveSubFlow);
        ui->menuEdit->insertSeparator(before);
    }
    connect(m_actionDefineSubFlow, &QAction::triggered, this, &MainWindow::onDefineSubFlow);
    connect(m_actionRemoveSubFlow, &QAction::triggered, this, &MainWindow::onRemoveSubFlow);
    connect(m_actionCreateGroup, &QAction::triggered, this, &MainWindow::onCreateGroup);
    connect(m_actionDissolveGroup, &QAction::triggered, this, &MainWindow::onDissolveGroup);
    connect(m_actionToggleGroup, &QAction::triggered, this, &MainWindow::onToggleGroupCollapse);

    // 语言入口在菜单收敛时挂到「帮助 → 语言」（实装 4.4 菜单栏没有独立顶层“语言”）。

    // 执行控制
    connect(ui->actionStartExecution, &QAction::triggered, this, &MainWindow::onStartExecution);
    connect(ui->actionStopExecution, &QAction::triggered, this, &MainWindow::onStopExecution);

    // 最近打开菜单（文件菜单下）：记录/去重/截断/清空全部由 RecentFilesMenu 负责，
    // 点击某条记录回调 loadProjectFile（与原实现一致）
    m_recentFiles = new RecentFilesMenu(ui->menuFile,
                                        [this](const QString &path) { loadProjectFile(path); },
                                        QString(), this);

    // 帮助菜单（软件内查看操作手册）
    {
        m_helpMenu = new QMenu(tr("帮助"), this);
        ui->menuBar->addMenu(m_helpMenu);
        // 菜单顺序按 VM 4.4 实际界面（2026-11-07 逐屏截图核对）：
        // 帮助文档 → 帮助文档（在线） → 版本信息 → 更多支持 → V社区 → 打开欢迎页；
        // 本实现额外的自检/关于放在分隔线之后。
        QAction *manualAct = m_helpMenu->addAction(tr("帮助文档"));
        manualAct->setShortcut(QKeySequence(Qt::Key_F1));
        connect(manualAct, &QAction::triggered, this, [this]() { openManualDialog(); });
        QAction *onlineDocAct = m_helpMenu->addAction(tr("帮助文档（在线）"));
        connect(onlineDocAct, &QAction::triggered, this, [this]() {
            const QUrl url(QStringLiteral("https://www.hikrobotics.com/cn/machinevision/service"));
            if (!QDesktopServices::openUrl(url))
                QMessageBox::information(this, tr("帮助文档（在线）"),
                                         tr("无法打开浏览器，请手动访问：\n%1").arg(url.toString()));
        });
        QAction *versionAct = m_helpMenu->addAction(tr("版本信息"));
        connect(versionAct, &QAction::triggered, this, [this]() {
            QMessageBox::information(
                this, tr("版本信息"),
                QStringLiteral("VisionFlowPlatform\n"
                               "版本：%1\n"
                               "Qt：%2\n"
                               "算法：OpenCV\n"
                               "图容器 / DeepOCR：HALCON 24.11\n"
                               "操作系统：%3")
                    .arg(QStringLiteral(VFP_VERSION_DISPLAY), QString::fromLatin1(qVersion()),
                         QSysInfo::prettyProductName()));
        });
        QAction *supportAct = m_helpMenu->addAction(tr("更多支持"));
        connect(supportAct, &QAction::triggered, this, [this]() {
            const QUrl url(QStringLiteral("https://www.hikrobotics.com/"));
            if (!QDesktopServices::openUrl(url))
                QMessageBox::information(this, tr("更多支持"),
                                         tr("无法打开浏览器，请手动访问：\n%1").arg(url.toString()));
        });
        QAction *communityAct = m_helpMenu->addAction(QStringLiteral("V社区"));
        connect(communityAct, &QAction::triggered, this, [this]() {
            const QUrl url(QStringLiteral("https://www.v-club.com"));
            if (!QDesktopServices::openUrl(url))
                QMessageBox::information(this, QStringLiteral("V社区"),
                                         tr("无法打开浏览器，请手动访问：\n%1").arg(url.toString()));
        });
        QAction *welcomeAct = m_helpMenu->addAction(tr("打开欢迎页"));
        connect(welcomeAct, &QAction::triggered, this, [this]() { showWelcomePage(); });
        m_helpMenu->addSeparator();
        QAction *envCheckAct = m_helpMenu->addAction(QStringLiteral("HALCON 图像层自检"));
        connect(envCheckAct, &QAction::triggered, this, [this]() { runHalconEnvCheck(); });
        QAction *aboutAct = m_helpMenu->addAction(tr("关于"));
        connect(aboutAct, &QAction::triggered, this, [this]() {
            // 版本串由 CMake 注入（cmake/Version.h.in）：版本号 + 提交短哈希 + 配置日期，
            // 现场报障时能直接说出装的是哪一版（#520）。
            QMessageBox::about(this, tr("关于 VisionFlowPlatform"),
                               QStringLiteral(VFP_VERSION_DISPLAY "\n"
                                              "对标 VisionMaster 4.4 的视觉流程开发平台\n"
                                              "算法：OpenCV\n"
                                              "图容器 / DeepOCR：HALCON 24.11"));
        });
    }
}

void MainWindow::hookFlowScene(FlowScene *scene)
{
    if (!scene) {
        return;
    }
    connect(scene, &FlowScene::nodeSelected, this, &MainWindow::onNodeSelected);
    connect(scene, &FlowScene::nodeAdded, this, &MainWindow::onNodeAdded);
    connect(scene, &FlowScene::nodeRemoved, this, &MainWindow::onNodeRemoved);
    // 状态栏「等待添加模块 / 就绪」随模块增减刷新（与上方两个槽并挂，不改槽内职责）
    connect(scene, &FlowScene::nodeAdded, this, [this]() { updateStatusHint(); });
    connect(scene, &FlowScene::nodeRemoved, this, [this]() { updateStatusHint(); });
    // 撤销/重做按钮可用态跟随当前流程栈（P1-5）
    connect(scene, &FlowScene::undoAvailable, this, [this, scene](bool available) {
        if (m_undoAction && scene == currentFlowScene())
            m_undoAction->setEnabled(available);
    });
    connect(scene, &FlowScene::redoAvailable, this, [this, scene](bool available) {
        if (m_redoAction && scene == currentFlowScene())
            m_redoAction->setEnabled(available);
    });
    // 画布聚焦时的 Ctrl+C / Ctrl+V：场景只发请求，剪贴板与粘贴位置都归窗口管
    // （参数框/日志里的 Ctrl+C 因此仍是原生文本复制——那些控件不经过场景）
    connect(scene, &FlowScene::copySelectionRequested, this, [this, scene]() {
        copySelectionToClipboard(scene);
    });
    connect(scene, &FlowScene::pasteRequested, this, [this, scene]() {
        pasteSnippetIntoScene(scene);
    });
    connect(scene, &FlowScene::connectionRejected, this, [this](const QString &reason) {
        logMessage(reason);
    });

    connect(scene, &FlowScene::nodeHelpRequested, this, [this](NodeBase *node) {
        if (!m_helpViewer)
            m_helpViewer = new HelpViewer(this);
        m_helpViewer->showHelpForNode(node);
    });
    connect(scene, &FlowScene::nodeOutputDataRequested, this, [this](NodeBase *node) {
        onOpenOutputViewer();
        if (auto *ov = m_auxPanels->outputDataViewer())
            ov->setNode(node);
    });
    // 右键「重算此算子及下游」：调试时只重跑这一段，不必整图重跑
    connect(scene, &FlowScene::recomputeFromRequested, this, [this](NodeBase *node) {
        recomputeDownstream(node);
    });
    connect(scene, &FlowScene::executeToHereRequested, this, [this](NodeBase *node) {
        int idx = ui->flowTabs->currentIndex();
        if (idx < 0 || idx >= m_flowScenes.size())
            return;
        FlowExecutor *ex = executorForScene(m_flowScenes[idx]);
        if (ex)
            runWithBusyFeedback(QStringLiteral("正在执行到 %1…").arg(node->fullName()), false,
                                [ex, node]() { ex->executeUpTo(node); });
    });
    connect(scene, &FlowScene::executeFromHereRequested, this, [this](NodeBase *node) {
        int idx = ui->flowTabs->currentIndex();
        if (idx < 0 || idx >= m_flowScenes.size())
            return;
        FlowExecutor *ex = executorForScene(m_flowScenes[idx]);
        if (ex)
            runWithBusyFeedback(QStringLiteral("正在从此处执行 %1…").arg(node->fullName()), false,
                                [ex, node]() { ex->executeFrom(node); });
    });
    connect(scene, &FlowScene::nodeEditRequested, this, &MainWindow::openModuleEditor);
}

void MainWindow::createNewFlow()
{
    try {
        FlowScene *scene = new FlowScene(this);
        if (!scene) {
            VFP_DEBUG << "Failed to create FlowScene";
            return;
        }
        
        hookFlowScene(scene);

        m_flowScenes.append(scene);
        
        // 添加到标签页
        // 注意：FlowScene继承自QGraphicsScene，需要使用views()获取QGraphicsView
        FlowCanvasView *view = new FlowCanvasView(scene);
        VisionWorkbenchStyle::applyGraphicsViewWorkbenchDefaults(view);
        view->setDragMode(QGraphicsView::RubberBandDrag);
        view->setRubberBandSelectionMode(Qt::IntersectsItemShape);
        view->setFocusPolicy(Qt::StrongFocus);
        // P2：状态栏缩放百分比只跟随当前流程画布（其它流程的缩放变化不串台）
        connect(view, &FlowCanvasView::zoomPercentChanged, this, [this, view](int percent) {
            if (m_zoomStatusLabel && view == canvasViewForScene(currentFlowScene()))
                m_zoomStatusLabel->setText(tr("缩放 %1%").arg(percent));
        });
        // 默认视图定位到左上角（延迟到布局完成后执行）
        // U-20（2026-11-10）：context 必须是被捕获的 view 本身，不能用长寿命的 MainWindow——
        // 流程页关闭时 view 被 removeTab 后立即 delete，MainWindow 不会取消这条排队定时器。
        QTimer::singleShot(0, view, [view]() {
            view->centerOn(0, 0);
        });
        // 分配全局唯一流程名：扫描 GTM 已注册名，返回首个空闲"流程 N"。避免删除流程后
        // 用 m_flowScenes.size() 复用序号 → 同名覆盖仍存流程的触发路由（L2 存量）。
        const QString flowName = GlobalTriggerManager::instance()->allocFlowName();
        scene->setFlowName(flowName);   // S2：写入场景，保存方案时随流程持久化（载入按名复原）

        if (ui && ui->flowTabs) {
            int index = ui->flowTabs->addTab(view, flowName);
            // 初始标签页标题带默认模式后缀（与 GTM 路由名一致）
            ui->flowTabs->setTabText(index, flowName + QStringLiteral(" [软触发]"));
            ui->flowTabs->setCurrentIndex(index);
        } else {
            VFP_DEBUG << "ui or ui->flowTabs is null";
            delete view;
        }

        // 设置执行器的流程场景（多流程并发：每个流程独立执行器）
        FlowExecutor *flowEx = executorForScene(scene);
        m_executor = flowEx;
        if (flowEx) {
            flowEx->setFlowScene(scene);
            // 注册流程名到全局触发管理器（与 tab 标题同名，触发按名路由）
            flowEx->setFlowName(flowName);
            // P3c：绑定持令牌（弱引用）——executorForScene 已把令牌落在场景键上
            GlobalTriggerManager::instance()->registerFlow(flowName, scene,
                                                           m_flowExecutors.value(scene));
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in createNewFlow:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in createNewFlow";
    }
}

void MainWindow::addNode(const QString &nodeType)
{
    try {
        if (!ui || !ui->flowTabs) {
            VFP_DEBUG << "ui or ui->flowTabs is null";
            return;
        }
        
        int currentIndex = ui->flowTabs->currentIndex();
        if (currentIndex < 0 || currentIndex >= m_flowScenes.size()) {
            return;
        }
        
        FlowScene *scene = m_flowScenes[currentIndex];
        if (!scene) {
            VFP_DEBUG << "scene is null";
            return;
        }

        // 双击工具库入画布必须与拖拽用同一把锁：FlowScene::dragEnter/dragMove/drop 都判
        // m_editLocked，而这条路径直接 createNode（createNode 本身不查锁），于是锁定时
        // "拖不进、双击能进"，进去后节点又不可动——体验上就是"工具坏了"。
        // 这里显式拦下并给出原因，而不是静默建一个动不了的节点（2026-10-24 现场反馈）。
        if (scene->isEditLocked()) {
            if (ui->statusBar) {
                auto *session = SessionManager::instance();
                // 锁是「运行态 || 写闸」的组合（见 SessionManager::canvasEditShouldBeLocked）。
                // 若一律提示"先停止执行"，写闸生效时用户照做也永远解不开——下一步动作必须
                // 跟着真实来源走。写闸文案复用 writeGateRefusal()（唯一来源），只补菜单路径。
                const QString refusal = session->writeGateRefusal();
                ui->statusBar->showMessage(
                    !refusal.isEmpty()
                        ? refusal + tr("（菜单：系统 → 修改管理员口令）")
                        : tr("画布已锁定（流程运行中）——请先停止执行，再双击/拖拽添加算子"),
                    8000);
            }
            return;
        }

        NodeBase *node = nullptr;

        const QPointF pos(100, 100);
        NodeBase::NodeType nt = NodeBase::IMAGE_ACQUISITION;
        QString createName;
        if (NodeFactory::resolvePaletteToolId(nodeType, nt, createName)) {
            node = scene->createNode(nt, pos, createName);
        }

        if (node) {
            // 节点已创建，可以在这里添加额外的初始化代码
            VFP_DEBUG << "Node created:" << node->name();
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in addNode:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in addNode";
    }
}

void MainWindow::onNodeSelected(NodeBase *node)
{
    try {
        VFP_DEBUG << "onNodeSelected called";
        m_selectedNode = node;
        // P2-6：图像区本地图列表随选中读图模块联动
        refreshLocalImagePanel(node);
        // 实装 4.4 图像区①区：图像源下拉随选中刷新
        refreshImageSourceCombo(node);
        
        if (node) {
            // 选中算子即显示它的中间结果图（对标 VisionMaster 的操作性：点哪个模块就看哪个
            // 模块的图，而不用先去执行它）。数据直接取自节点输出端口，无需改执行引擎；
            // 尚未执行、没有可用图像时保持当前画面不动，避免把已有画面清空。
            m_imageDisplay->displayNodeOutput(node);
            // B3：选中即在主画布上挂出该算子的几何 ROI（可直接拖角点/中心改，运行中也可改）
            m_imageDisplay->showNodeGeometry(resolveDisplayNode(node));

            // 测量节点画布取点：连接 ROI 按钮信号（断开旧连接防重复）
            if (m_roiPickConn) {
                QObject::disconnect(m_roiPickConn);
                m_roiPickConn = QMetaObject::Connection();
            }
            if (auto *fl = qobject_cast<FindLineNode *>(node)) {
                m_roiPickConn = connect(fl, &FindLineNode::roiPickRequested, this,
                                        [this, fl]() { startRoiPick(fl); });
            } else if (auto *fc = qobject_cast<FindCircleNode *>(node)) {
                m_roiPickConn = connect(fc, &FindCircleNode::roiPickRequested, this,
                                        [this, fc]() { startRoiPick(fc); });
            } else if (auto *cc = qobject_cast<CaliperMeasureNode *>(node)) {
                m_roiPickConn = connect(cc, &CaliperMeasureNode::roiPickRequested, this,
                                        [this, cc]() { startRoiPick(cc); });
            }

            // 延迟创建参数面板，避免阻塞主线程。QPointer 守卫：节点可能在 50ms 内被删除，
            // 裸指针捕获会让定时器回调直接踩已析构对象。
            QPointer<NodeBase> nodeGuard(node);
            QTimer::singleShot(50, this, [this, nodeGuard]() {
                if (!nodeGuard)
                    return;
                showNodeParameters(nodeGuard.data());
            });
            
            // 显示图像前检查优先级规则：下拉框选择 > 画布选中
            if (m_imageView) {
                QTimer::singleShot(100, this, [this, nodeGuard]() {
                    if (!nodeGuard)
                        return;
                    NodeBase *node = nodeGuard.data();
                    // resolveDisplayNode(node) 只检查下拉框选择，不回退到 m_selectedNode
                    // 因为此处我们本身就是因画布选中触发的，如果下拉框无选择才显示本节点
                    NodeBase *target = resolveDisplayNode(node);
                    if (!target)
                        return;
                    if (auto outputData = target->getOutputData(0)) {
                        const HImage image = outputData->getHImage();
                        // 与原实现一致：此路径只换图像与来源标签，不动叠加图元
                        m_imageDisplay->showImage(image, target->fullName());
                    }
                    // B3：显示源换了，ROI 也要跟着换成该节点的几何
                    m_imageDisplay->showNodeGeometry(target);
                });
            }
        } else if (m_imageDisplay) {
            // 取消选中：收起可编辑 ROI，避免继续对着"上一个人的框"改参数
            m_imageDisplay->showNodeGeometry(nullptr);
        }

        // P1-2：结果区跟随画布选中（对标 VM：点哪个模块，结果区就切到哪个模块）。
        // 图像区「模块结果」常驻显示当前轮结果，选中只做高亮/滚动，不再强弹底部面板。
        if (node) {
            for (ResultTablePanel *rt : resultPanels()) {
                rt->setCurrentModule(node->moduleId());
                rt->setHelpContent(buildNodeHelpHtml(node));
            }
            // 实装 4.4「结果显示区域」：采集 / 逻辑工具在**底部历史区**只展示历史结果；
            // 图像区「模块结果」是当前轮结果视图，不参与该切换（否则该页会被整页隐藏）。
            if (ResultTablePanel *history =
                    m_auxPanels ? m_auxPanels->resultTablePanel() : nullptr) {
                history->setHistoryOnlySelection(node->type() == NodeBase::IMAGE_ACQUISITION
                                                 || node->type() == NodeBase::LOGIC);
            }
        } else {
            for (ResultTablePanel *rt : resultPanels()) {
                rt->setCurrentModule(-1);
                rt->setHelpContent(buildNodeHelpHtml(nullptr));
            }
            if (ResultTablePanel *history =
                    m_auxPanels ? m_auxPanels->resultTablePanel() : nullptr)
                history->setHistoryOnlySelection(false);
        }
        // P2-4：画布右下角角标显示选中模块的最近耗时
        updateCanvasSelectionElapsed(node);
        // P2-5：若勾选了「叠加上游」，切换模块后重新叠加
        if (!m_overlayNodes.isEmpty())
            applyExtraOverlays(node);
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in onNodeSelected:" << e.what();
    }
}

void MainWindow::showNodeParameters(NodeBase *node)
{
    QWidget *paramWidget = ui->paramDockContents;
    if (!paramWidget)
        return;

    // 清空旧内容（含未选中时的占位）
    if (QLayout *old = paramWidget->layout()) {
        QLayoutItem *item = nullptr;
        while ((item = old->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete old;
    }

    auto *box = new QVBoxLayout(paramWidget);
    box->setContentsMargins(8, 8, 8, 8);

    if (!node) {
        // FR4.3：参数页只显示「当前选中算子」的参数。未选中时给出明确占位，
        // 避免残留上一个算子的参数被误读为当前配置。
        auto *placeholder = new QLabel(QStringLiteral("请选择算子以查看参数"));
        placeholder->setWordWrap(true);
        placeholder->setStyleSheet(QStringLiteral("color:#9a9aa6;"));
        box->addWidget(placeholder);
        box->addStretch();
        return;
    }

    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(node->fullName()));
    title->setWordWrap(true);
    box->addWidget(title);

    const QString status = !node->hasExecuted()
        ? QStringLiteral("尚未执行")
        : (node->executionSuccess() ? QStringLiteral("上次执行：成功")
                                    : QStringLiteral("上次执行：失败"));
    box->addWidget(new QLabel(status));

    // 运行中改参数是允许的（设计如此），但不提示会让现场以为"改了没生效"——
    // 参数写入线程安全、下一轮生效，这句话必须写在参数旁边而不是只写在文档里。
    // 例外：MVS 相机的曝光/增益/像素格式要写硬件（2026-10-25 复核 P3-9）——必须点
    // "应用参数到相机"才下发，且运行中像素格式会被跳过；对它们说"下一轮生效"是假话。
    const bool isMvsCamera = qobject_cast<MvsImageSourceNode *>(node) != nullptr;
    const QString hintText = isMvsCamera
        ? (m_currentLanguage == Language::Chinese
               ? QStringLiteral("提示：相机参数需点「应用参数到相机」才写入；运行中像素格式会被跳过")
               : QStringLiteral("Note: camera parameters are written only by \"Apply to camera\"; "
                                "pixel format is skipped while running"))
        : (m_currentLanguage == Language::Chinese
               ? QStringLiteral("提示：运行中修改参数将于下一轮生效")
               : QStringLiteral("Note: parameter changes take effect next cycle"));
    auto *hint = new QLabel(hintText);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#9a9aa6;"));
    box->addWidget(hint);

    if (auto *hn = qobject_cast<HalconNode *>(node)) {
        if (hn->geometryRoiType() != RoiType::None)
            box->addWidget(new QLabel(QStringLiteral("双击可在图上编辑 ROI / 模板 / 卡尺")));
        else if (hn->supportsMaskEdit())
            box->addWidget(new QLabel(QStringLiteral("双击可编辑掩膜")));
        else
            box->addWidget(new QLabel(QStringLiteral("双击打开完整参数")));

        // B3：主画布直接改搜索区域（不必打开模块编辑窗）；运行中改也安全，下一轮生效。
        if (ImageDisplayController::hasSearchRegion(node) && m_imageDisplay) {
            auto *roiTarget = new QCheckBox(QStringLiteral("主画布编辑：搜索区域"));
            roiTarget->setChecked(m_imageDisplay->editingSearchRegion());
            roiTarget->setToolTip(QStringLiteral(
                "勾选后主画布上的可编辑框 = 搜索区域（青色）；取消则回到模板框。"
                "拖角点/中心即可改，参数立即回写，运行中下一轮生效。"));
            connect(roiTarget, &QCheckBox::toggled, this, [this, node](bool on) {
                if (!m_imageDisplay)
                    return;
                if (on)
                    m_imageDisplay->showNodeSearchRegion(node);
                else
                    m_imageDisplay->showNodeGeometry(node, /*preferSearchRegion=*/false);
            });
            box->addWidget(roiTarget);
        }

        int shown = 0;
        for (const ParamSpec &spec : hn->paramSpecs()) {
            if (shown >= 6)
                break;
            const QString label = spec.label.isEmpty() ? spec.name : spec.label;
            box->addWidget(new QLabel(QStringLiteral("%1：%2")
                                          .arg(label, hn->getParam(spec.name).toString())));
            ++shown;
        }
    } else {
        box->addWidget(new QLabel(QStringLiteral("双击打开完整参数")));
    }

    auto *editBtn = new QPushButton(QStringLiteral("打开编辑器"));
    connect(editBtn, &QPushButton::clicked, this, [this, node]() { openModuleEditor(node); });
    box->addWidget(editBtn);

    auto *execBtn = new QPushButton(QStringLiteral("执行算子"));
    connect(execBtn, &QPushButton::clicked, this, [this, node]() { executeNodeOnce(node); });
    box->addWidget(execBtn);

    box->addWidget(new QLabel(m_currentLanguage == Language::Chinese
                                  ? QStringLiteral("图像显示来源:")
                                  : QStringLiteral("Display source:")));
    auto *combo = new QComboBox();
    combo->setObjectName(QStringLiteral("paramImageSourceCombo"));
    combo->addItem(node->fullName(), QVariant::fromValue(node));

    const int currentIndex = ui->flowTabs->currentIndex();
    NodeBase *savedSelectedNode = nullptr;
    if (currentIndex >= 0 && currentIndex < m_flowScenes.size()) {
        FlowScene *scene = m_flowScenes[currentIndex];
        if (scene) {
            savedSelectedNode = m_imageDisplay->selectedOutputNode(scene);
            for (NodeBase *up : UpstreamWalk::upstreamOf(node)) {
                if (up && up != node)
                    combo->addItem(up->fullName(), QVariant::fromValue(up));
            }
        }
    }
    if (savedSelectedNode) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).value<NodeBase *>() == savedSelectedNode) {
                combo->setCurrentIndex(i);
                break;
            }
        }
    }
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, combo](int index) {
        NodeBase *selected = combo->itemData(index).value<NodeBase *>();
        if (!selected)
            return;
        const int idx = ui->flowTabs->currentIndex();
        if (idx >= 0 && idx < m_flowScenes.size())
            m_imageDisplay->setSelectedOutputNode(m_flowScenes[idx], selected);
        if (auto outputData = selected->getOutputData(0)) {
            const HImage image = outputData->getHImage();
            // 与原实现一致：此路径只换图像与来源标签，不动叠加图元
            m_imageDisplay->showImage(image, selected->fullName());
        }
    });
    box->addWidget(combo);
    box->addStretch();
}

void MainWindow::openModuleEditor(NodeBase *node)
{
    if (!node)
        return;
    // P0：写闸开启期间模块编辑面同禁。落库闸只拦「保存」，而「内存里改完参数/脚本立刻跑」
    // 这条路在重启前一直有效——闸要拦的正是这一格。已开着的编辑器一并收掉（锁不能从编辑器
    // 背后关上），拒因取写闸的单一判据串，不另写一份。
    const QString gateRefusal = SessionManager::instance()->writeGateRefusal();
    if (!gateRefusal.isEmpty()) {
        m_editors.closeIfPresent(node);   // 收窗顺带摘位（WA_DeleteOnClose 关窗即销毁走摘位连接）
        QMessageBox::warning(this, tr("模块编辑被禁止"), gateRefusal);
        return;
    }
    // P1-7：用户显式锁定当前流程时，模块编辑窗口同样禁止（与画布锁定同一把锁）。
    if (FlowScene *scene = currentFlowScene()) {
        if (scene->isUserLocked()) {
            QMessageBox::warning(this, tr("流程已锁定"),
                                 tr("当前流程已锁定，请先在工具栏解除锁定后再编辑模块。"));
            return;
        }
    }
    // 「一算子一窗」簿位在 ModuleEditorRegistry：命中回调只做复原/前置。
    // 信号连接只在**首次建窗**时建立：重复打开同一个算子时若再 connect 一遍，
    // 点一次「执行」会触发 N 次、点「确定」会重算 N 次（N = 打开次数）。
    bool isNewEditor = false;
    ModuleEditorDialog *dlg =
        qobject_cast<ModuleEditorDialog *>(m_editors.editorFor(
            node,
            [this, node, &isNewEditor]() -> QDialog * {
                isNewEditor = true;
                return new ModuleEditorDialog(node, this);
            },
            [](QWidget *window) {
                if (auto *editor = qobject_cast<ModuleEditorDialog *>(window)) {
                    editor->reloadFromNode();
                    editor->raise();
                    editor->activateWindow();
                }
            }));
    if (!dlg)
        return;
    if (!isNewEditor) {
        // 已存在的窗：连接仍在（首次建窗时建的那一份），这里只刷新变量引用并前置
        dlg->setVariableReferenceProvider([this]() { return buildVariableReferences(); });
        dlg->show();
        dlg->raise();
        return;
    }
    connect(dlg, &ModuleEditorDialog::executeRequested, this, [this, node]() {
        executeNodeOnce(node);
    });
    connect(dlg, &ModuleEditorDialog::recomputeRequested, this, [this, node]() {
        recomputeDownstream(node);
    });
    // 自动重算（改参数/拖 ROI/涂掩膜）：静默执行，流程忙时直接跳过
    connect(dlg, &ModuleEditorDialog::autoRecomputeRequested, this, [this, node, dlg]() {
        recomputeDownstream(node, true);
        // 自动重算后刷新结果叠加（r19 补全清单第 4 项）：与「执行算子」同款两行。
        // 缺这两行时，改参数/拖 ROI 后画布上看不到新的匹配框（现场反馈"没有运行时渲染"）。
        dlg->reloadFromNode();
        dlg->setResultOverlay(collectOverlayFromNode(node));
    });
    // P1-1：模块编辑窗左上角帮助入口（对标 VM「模块通用配置」的 ? 按钮）
    connect(dlg, &ModuleEditorDialog::helpRequested, this, [this, node](NodeBase *target) {
        if (!m_helpViewer)
            m_helpViewer = new HelpViewer(this);
        m_helpViewer->showHelpForNode(target ? target : node);
    });
    // 模块窗「绘制检测区域」→ 主画布拾取（VM 的模块窗是纯参数窗，绘制在图像窗口完成）
    connect(dlg, &ModuleEditorDialog::searchRegionPickRequested,
            this, &MainWindow::beginSearchRegionPick);
    // P1-8：模块窗「绘制屏蔽区」→ 主画布逐点绘制，追加到 maskRegionPolygon。
    connect(dlg, &ModuleEditorDialog::maskRegionPickRequested,
            this, &MainWindow::beginMaskRegionPick);
    // 模块窗「ROI 区域绘制」→ 主画布画模板框（框完再由「训练模板」显式训练）
    connect(dlg, &ModuleEditorDialog::templateRoiPickRequested,
            this, &MainWindow::beginTemplateRoiPick);
    // 检测区域每个输入框右侧的「取点」按钮 → 主画布单击取坐标
    connect(dlg, &ModuleEditorDialog::coordinatePickRequested,
            this, &MainWindow::beginCoordinatePick);
    // 通用几何 ROI / 掩膜：模块窗是纯页签窗，绘制与涂抹都落在主画布
    connect(dlg, &ModuleEditorDialog::geometryPickRequested,
            this, &MainWindow::beginGeometryPick);
    connect(dlg, &ModuleEditorDialog::maskEditRequested,
            this, &MainWindow::beginMaskEdit);
    connect(dlg, &ModuleEditorDialog::maskBrushRadiusChanged, this, [this](int radius) {
        if (m_imageView)
            m_imageView->setMaskBrushRadius(radius);
    });
    connect(dlg, &ModuleEditorDialog::maskEraseModeChanged, this, [this](bool erase) {
        if (m_imageView)
            m_imageView->setMaskPaintKeep(!erase);
    });
    // 「变量引用」菜单：每次展开时按当前变量生成列表（上游结果 → 下游参数的一键联动）
    dlg->setVariableReferenceProvider([this]() { return buildVariableReferences(); });
    dlg->show();
    dlg->raise();
}

QStringList MainWindow::buildVariableReferences() const
{
    // 变量域两出口之二：引用菜单在 RoundVariableFeed（文本经 VariablePanel 双静态，终排序兜序）
    return RoundVariableFeed::roundReferenceTexts(
        GlobalVariableManager::instance()->variables(), m_lastModuleVars);
}

QString MainWindow::buildNodeHelpHtml(NodeBase *node) const
{
    if (!node) {
        return QStringLiteral(
            "<p><b>帮助信息</b></p>"
            "<p>在流程编辑区选中某个模块后，这里显示该模块的分组、说明、"
            "输入/输出端口与运行参数。</p>");
    }

    const QString typeId = node->property("vfpNodeTypeId").toString();
    const NodeRegistration *reg = NodeRegistry::instance().findById(typeId);

    QString html;
    html += QStringLiteral("<p><b>%1</b></p>").arg(node->fullName().toHtmlEscaped());
    if (reg && !reg->group.isEmpty())
        html += QStringLiteral("<p>分组：%1</p>").arg(reg->group.toHtmlEscaped());
    if (reg && !reg->description.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(reg->description.toHtmlEscaped());

    html += QStringLiteral("<p><b>输入端口</b></p><ul>");
    const QList<Port *> inputs = node->inputPorts();
    if (inputs.isEmpty()) {
        html += QStringLiteral("<li>无</li>");
    } else {
        for (Port *port : inputs) {
            QString text = port->name().toHtmlEscaped();
            QStringList upstream;
            for (MyProject::Connection *conn : port->connections()) {
                if (!conn || !conn->getSourceNode())
                    continue;
                upstream << conn->getSourceNode()->fullName();
            }
            if (!upstream.isEmpty())
                text += QStringLiteral("（来自：%1）").arg(upstream.join(QStringLiteral("、")).toHtmlEscaped());
            html += QStringLiteral("<li>%1</li>").arg(text);
        }
    }
    html += QStringLiteral("</ul>");

    html += QStringLiteral("<p><b>输出端口</b></p><ul>");
    const QList<Port *> outputs = node->outputPorts();
    if (outputs.isEmpty()) {
        html += QStringLiteral("<li>无</li>");
    } else {
        for (Port *port : outputs)
            html += QStringLiteral("<li>%1</li>").arg(port->name().toHtmlEscaped());
    }
    html += QStringLiteral("</ul>");

    if (auto *halcon = qobject_cast<HalconNode *>(node)) {
        const ParamSpecList &specs = halcon->paramSpecs();
        if (!specs.isEmpty()) {
            html += QStringLiteral("<p><b>运行参数</b></p><ul>");
            int shown = 0;
            for (const ParamSpec &spec : specs) {
                const QString label = spec.label.isEmpty() ? spec.name : spec.label;
                html += QStringLiteral("<li>%1 = %2</li>")
                            .arg(label.toHtmlEscaped(),
                                 halcon->getParam(spec.name).toString().toHtmlEscaped());
                if (++shown >= 12) {
                    if (specs.size() > shown)
                        html += QStringLiteral("<li>…（共 %1 项，完整参数见模块配置窗）</li>")
                                    .arg(specs.size());
                    break;
                }
            }
            html += QStringLiteral("</ul>");
        }
    }

    return html;
}

void MainWindow::refreshImageSourceCombo(NodeBase *node)
{
    if (!m_imageSourceCombo)
        return;
    QSignalBlocker blocker(m_imageSourceCombo);
    m_imageSourceCombo->clear();
    if (!node) {
        m_imageSourceCombo->setEnabled(false);
        return;
    }
    m_imageSourceCombo->setEnabled(true);
    m_imageSourceCombo->addItem(node->fullName(), QVariant::fromValue(node));
    for (NodeBase *up : upstreamNodes(node)) {
        if (up && up != node)
            m_imageSourceCombo->addItem(up->fullName(), QVariant::fromValue(up));
    }

    // 恢复当前场景记忆的显示源（下拉框选择 > 画布选中）
    NodeBase *selected = nullptr;
    const int idx = ui->flowTabs->currentIndex();
    if (m_imageDisplay && idx >= 0 && idx < m_flowScenes.size())
        selected = m_imageDisplay->selectedOutputNode(m_flowScenes[idx]);
    if (selected) {
        for (int i = 0; i < m_imageSourceCombo->count(); ++i) {
            if (m_imageSourceCombo->itemData(i).value<NodeBase *>() == selected) {
                m_imageSourceCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

void MainWindow::openAuxPanel(const QString &key)
{
    // 创建/显示/刷新统一委托 AuxPanelManager；本函数保留"面板打开后的窗口级副作用"
    // （日志提示等），并作为菜单/启动恢复的统一入口。
    if (key == QStringLiteral("resultTable")) {
        onOpenResultTable();
        return;
    }
    if (key == QStringLiteral("performance")) {
        onOpenPerformancePanel();
        return;
    }
    if (key == QStringLiteral("outputData")) {
        onOpenOutputViewer();
        return;
    }
    if (key == QStringLiteral("variable")) {
        m_auxPanels->open(key);
        logMessage(QStringLiteral("变量面板已打开：双击某行即可复制引用表达式"));
        return;
    }
}

QVector<ResultTablePanel *> MainWindow::resultPanels() const
{
    QVector<ResultTablePanel *> panels;
    // 图像区「模块结果」常驻（构造即创建）；底部结果表只在用户打开过之后存在。
    if (m_moduleResultPanel)
        panels.append(m_moduleResultPanel);
    if (m_auxPanels) {
        if (ResultTablePanel *rt = m_auxPanels->resultTablePanel())
            panels.append(rt);
    }
    return panels;
}

void MainWindow::saveAuxPanelVisibility() const
{
    // 显隐记录（QSettings 键名契约）由 AuxPanelManager 经 PanelVisibilityStore 完成
    m_auxPanels->saveVisibility();
}

void MainWindow::restoreAuxPanelVisibility()
{
    // 只恢复「显隐」，不恢复几何（理由见 AuxPanelManager::restoreVisibility）。
    // 启动恢复阶段执行器尚未创建，性能面板的 bindExecutor 由执行器创建处补做。
    m_auxPanels->restoreVisibility();
}

bool MainWindow::runWithBusyFeedback(const QString &what, bool quiet,
                                     const std::function<void()> &fn)
{
    // executeUpTo/executeFrom/executeNode 是同步执行（占用 UI 线程）：
    // 链路含耗时代工时，界面在期间不刷新——先绘制"正在执行"反馈，避免看起来像卡死。
    if (m_busyExecuting) {
        if (!quiet)
            logMessage(QStringLiteral("正在执行中，已忽略重复触发：%1").arg(what));
        return false;
    }
    m_busyExecuting = true;
    if (!quiet) {
        if (ui && ui->statusBar)
            ui->statusBar->showMessage(what);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QApplication::processEvents();   // 先把提示/光标绘制出来，再进入同步执行
    }
    fn();
    if (!quiet) {
        QApplication::restoreOverrideCursor();
        if (ui && ui->statusBar)
            ui->statusBar->clearMessage();
    }
    m_busyExecuting = false;
    return true;
}

void MainWindow::applyParamSubscriptions(FlowScene *scene)
{
    if (!scene)
        return;
    const QMap<QString, GlobalVariableManager::Variable> vars =
        GlobalVariableManager::instance()->variables();
    QVariantMap globals;
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        globals.insert(it.key(), it.value().value);
    const QString prefix = QStringLiteral("__sub__");

    for (NodeBase *node : scene->nodes()) {
        if (!node)
            continue;
        for (const QString &key : node->getAllParamNames()) {
            if (!key.startsWith(prefix))
                continue;
            const QString target = key.mid(prefix.size());
            QString expression = node->getParam(key).toString().trimmed();
            if (expression.isEmpty())
                continue;

            // 允许省略花括号；解析失败保持原值（不把参数写成空）
            if (expression.startsWith(QLatin1Char('{')) && expression.endsWith(QLatin1Char('}')))
                expression = expression.mid(1, expression.size() - 2).trimmed();

            QVariant value;
            if (expression.startsWith(QStringLiteral("global."))) {
                value = globals.value(expression.mid(7));
            } else if (expression.contains(QLatin1Char('.'))) {
                bool ok = false;
                const int sourceId = expression.section(QLatin1Char('.'), 0, 0).toInt(&ok);
                if (ok)
                    value = m_lastModuleVars.value(sourceId)
                                .value(expression.section(QLatin1Char('.'), 1));
            } else {
                value = globals.value(expression);
            }
            if (value.isValid() && !value.isNull())
                node->setParam(target, value);
        }
    }
}

void MainWindow::recomputeDownstream(NodeBase *node, bool quiet)
{
    if (!node)
        return;
    // 175 行拍板（P0-1①收口的完整义）：右键「重算此算子及下游」与模块编辑器重算/自动重算
    // 共用此漏斗，executeFrom 是一次真实的新发起，随闸收；quiet 面静默回退不扰自动重算。
    if (!SessionManager::instance()->canRunFlow()) {
        if (!quiet)
            logMessage(tr("写闸开启期间不受理重算下游：出厂默认口令仍在使用（改密后恢复）"));
        return;
    }

    const int idx = ui->flowTabs->currentIndex();
    if (idx < 0 || idx >= m_flowScenes.size())
        return;
    FlowExecutor *ex = executorForScene(m_flowScenes[idx]);
    if (!ex)
        return;

    const ExecutionState st = ex->getState();
    if (st == ExecutionState::Running || st == ExecutionState::Paused) {
        if (!quiet)
            logMessage(QStringLiteral("流程正在运行（或暂停）中，已取消「重算下游」；请先停止流程"));
        return;
    }

    // 先作废本算子及其下游的缓存，再执行这一段链路：不作废会让下游读到上一轮的旧值
    // （与 E2/P2 同类问题：本轮无输出/参数变更后必须让下游看到新值）。
    if (!runWithBusyFeedback(QStringLiteral("正在重算 %1 及其下游…").arg(node->fullName()),
                             quiet, [ex, node]() {
        ex->invalidateDownstreamOf(node);
        ex->executeFrom(node);
    }))
        return;
    if (!quiet)
        logMessage(QStringLiteral("已重算 %1 及其下游（未涉及的分支不重跑）").arg(node->fullName()));

    // 与「执行此算子」保持一致：把结果刷到图像窗口，便于立刻核对
    NodeBase *target = resolveDisplayNode(node);
    if (target && m_imageView)
        m_imageDisplay->displayNodeOutput(target);
}

void MainWindow::executeNodeOnce(NodeBase *node)
{
    if (!node)
        return;
    // 175 行拍板（P0-1①收口）：参数坞「执行算子」钮与模块编辑器「执行」钮共用此漏斗，
    // 单算子执行属新一次运行发起，随闸收（与运行入口七点同判据 canRunFlow）。
    if (!SessionManager::instance()->canRunFlow()) {
        logMessage(tr("写闸开启期间不受理单算子执行：出厂默认口令仍在使用（改密后恢复）"));
        return;
    }
    // 与 recomputeDownstream 同口径（复核 P1-5）：流程 Running/Paused 时单算子执行会在
    // GUI 线程与执行线程并发读写同一节点的参数表、输出 Mat 与 HALCON 句柄。原先这里
    // 只靠 runWithBusyFeedback 防 GUI 侧重入，拦不住"流程正在跑 + 用户点执行"。
    {
        const int stateIdx = ui->flowTabs->currentIndex();
        if (stateIdx >= 0 && stateIdx < m_flowScenes.size()) {
            if (FlowExecutor *sceneExec = executorForScene(m_flowScenes[stateIdx])) {
                const ExecutionState st = sceneExec->getState();
                if (st == ExecutionState::Running || st == ExecutionState::Paused) {
                    logMessage(tr("流程正在运行（或暂停）中，已取消「执行算子」；请先停止流程"));
                    return;
                }
            }
        }
    }
    bool success = false;
    if (!runWithBusyFeedback(QStringLiteral("正在执行 %1…").arg(node->fullName()), false,
                             [this, node, &success]() {
        const int currentIndex = ui->flowTabs->currentIndex();
        if (currentIndex >= 0 && currentIndex < m_flowScenes.size()) {
            FlowScene *currentScene = m_flowScenes[currentIndex];
            m_executor = executorForScene(currentScene);
            if (m_executor)
                m_executor->propagateData(node);
        }
        success = node->execute();
    }))
        return;   // 执行中重复触发被忽略（已记日志）
    logMessage(success ? QStringLiteral("算子执行成功") : QStringLiteral("算子执行失败"));
    if (!success)
        return;

    NodeBase *target = resolveDisplayNode(node);
    if (target && m_imageView)
        m_imageDisplay->displayNodeOutput(target);
    if (m_selectedNode == node)
        showNodeParameters(node);
    if (QWidget *editorWidget = m_editors.lookup(node)) {
        if (auto *dlg = qobject_cast<ModuleEditorDialog *>(editorWidget)) {
            dlg->reloadFromNode();
            dlg->setResultOverlay(collectOverlayFromNode(node));
        }
    }
}

void MainWindow::onNodeAdded(NodeBase *node)
{
    // 节点添加时的处理
    VFP_DEBUG << "Node added:" << node->name();
    
    // 连接ImageReadNode的imageRead信号 - 更新图像显示
    ImageReadNode *imageReadNode = dynamic_cast<ImageReadNode*>(node);
    if (imageReadNode) {
        connect(imageReadNode, &ImageReadNode::imageRead, this, [this, imageReadNode](const HalconCpp::HImage &image) {
            VFP_DEBUG << "ImageReadNode emitted imageRead signal";
            // 使用统一规则：下拉框选择 > 画布选中 > 兜底
            NodeBase *target = resolveDisplayNode(imageReadNode);
            if (target == imageReadNode && m_imageView) {
                m_imageView->requestImage(image, imageReadNode->fullName());
                if (m_imageSourceLabel) {
                    m_imageSourceLabel->setText(QString("图像来源: %1").arg(imageReadNode->fullName()));
                }
            }
        });
        
        // 连接ImageReadNode的thumbnailUpdated信号 - 更新缩略图
        connect(imageReadNode, &ImageReadNode::thumbnailUpdated, this, [this, imageReadNode]() {
            VFP_DEBUG << "ImageReadNode emitted thumbnailUpdated signal";
            // 检查参数面板是否存在
            QWidget *parameterPanel = ui->paramDockContents;
            if (parameterPanel) {
                // 查找参数面板中的所有子控件，寻找缩略图布局
                QList<QWidget*> childWidgets = parameterPanel->findChildren<QWidget*>();
                for (QWidget *childWidget : childWidgets) {
                    // 查找滚动区域，缩略图可能在滚动区域中
                    QScrollArea *scrollArea = qobject_cast<QScrollArea*>(childWidget);
                    if (scrollArea) {
                        QWidget *scrollContent = scrollArea->widget();
                        if (scrollContent) {
                            QHBoxLayout *thumbnailLayout = qobject_cast<QHBoxLayout*>(scrollContent->layout());
                            if (thumbnailLayout) {
                                // 直接更新缩略图
                                imageReadNode->updateThumbnails(thumbnailLayout);
                                VFP_DEBUG << "缩略图已更新，黄色框应该显示";
                                return;
                            }
                        }
                    }
                }
            }
        });
        VFP_DEBUG << "Connected ImageReadNode signals";
    }
    
    // 连接MvsImageSourceNode的imageAcquired信号 - 只用于调试，不自动更新显示
    MvsImageSourceNode *mvsImageSourceNode = dynamic_cast<MvsImageSourceNode*>(node);
    if (mvsImageSourceNode) {
        connect(mvsImageSourceNode, &MvsImageSourceNode::imageAcquired, this, [this]() {
            VFP_DEBUG << "MvsImageSourceNode emitted imageAcquired signal";
        });
        // 连接logMessage信号
        connect(mvsImageSourceNode, &MvsImageSourceNode::logMessage, this, &MainWindow::logMessage);
        VFP_DEBUG << "Connected MvsImageSourceNode signal";
    }
    
    // 连接HalconImageSourceNode的imageAcquired信号 - 只用于调试，不自动更新显示
    HalconImageSourceNode *halconImageSourceNode = dynamic_cast<HalconImageSourceNode*>(node);
    if (halconImageSourceNode) {
        connect(halconImageSourceNode, &HalconImageSourceNode::imageAcquired, this, [this](const HalconCpp::HImage &image) {
            VFP_DEBUG << "HalconImageSourceNode emitted imageAcquired signal";
        });
        VFP_DEBUG << "Connected HalconImageSourceNode signal";
    }
    
    // 连接ColorConversionNode的图像显示 - 只用于调试，不自动更新显示
    ColorConversionNode *colorConversionNode = dynamic_cast<ColorConversionNode*>(node);
    if (colorConversionNode) {
        connect(colorConversionNode, &ColorConversionNode::imageConverted, this, [this, colorConversionNode](const HalconCpp::HImage &image) {
            VFP_DEBUG << "ColorConversionNode emitted imageConverted signal";
        });
        VFP_DEBUG << "Connected ColorConversionNode signal";
    }
}

void MainWindow::onNodeRemoved(NodeBase *node)
{
    if (!node)
        return;

    // 编辑窗内持节点裸指针：节点销毁前先关窗（窗的 WA_DeleteOnClose 会走登记簿摘位）
    m_editors.closeIfPresent(node);
    // 输出数据查看器是非模态面板，也可能正看着该节点
    if (m_auxPanels) {
        if (auto *ov = m_auxPanels->outputDataViewer(); ov && ov->currentNode() == node)
            ov->setNode(nullptr);
    }
    m_overlayNodes.remove(node);
    m_nodeElapsed.remove(node);
    m_nodeParameters.remove(node);

    // 显示控制器里按场景记忆的显示源、画布 ROI 取点/几何编辑目标都可能是它
    if (m_imageDisplay)
        m_imageDisplay->forgetNode(node);

    if (m_localImageNode == node) {
        m_localImageNode = nullptr;
        if (m_localImageGroup)
            m_localImageGroup->setVisible(false);
        if (m_localImageList)
            m_localImageList->clear();
    }

    if (m_selectedNode == node) {
        m_selectedNode = nullptr;
        if (m_roiPickConn) {
            QObject::disconnect(m_roiPickConn);
            m_roiPickConn = QMetaObject::Connection();
        }
        if (m_imageDisplay)
            m_imageDisplay->showNodeGeometry(nullptr);
        showNodeParameters(nullptr);
        refreshImageSourceCombo(nullptr);
        refreshLocalImagePanel(nullptr);
        updateCanvasSelectionElapsed(nullptr);
        for (ResultTablePanel *rt : resultPanels()) {
            rt->setCurrentModule(-1);
            rt->setHelpContent(buildNodeHelpHtml(nullptr));
        }
        if (ResultTablePanel *history = m_auxPanels ? m_auxPanels->resultTablePanel() : nullptr)
            history->setHistoryOnlySelection(false);
    } else {
        // 被删节点可能是当前选中节点的上游：图像源下拉/叠加候选里存的是它的裸指针，
        // 必须重建一次，否则下次展开/切换就会解引用已析构对象。
        refreshImageSourceCombo(m_selectedNode);
        // 参数面板里的“图像源”下拉同样存上游裸指针：只有它真列了被删节点时才重建整页，
        // 避免“删任意节点都重建参数面板”带来的无谓开销。
        if (m_selectedNode && ui->paramDockContents) {
            bool stale = false;
            if (QComboBox *c = ui->paramDockContents->findChild<QComboBox *>(
                    QStringLiteral("paramImageSourceCombo"))) {
                for (int i = 0; i < c->count(); ++i) {
                    if (c->itemData(i).value<NodeBase *>() == node) {
                        stale = true;
                        break;
                    }
                }
            }
            if (stale)
                showNodeParameters(m_selectedNode);
        }
        if (m_selectedNode && !m_overlayNodes.isEmpty())
            applyExtraOverlays(m_selectedNode);
    }
}

void MainWindow::onImageRead(const HalconCpp::HImage &image)
{
    // 显示读取的图像
    m_imageView->requestImage(image, "读取图像");
    if (m_imageSourceLabel) {
        m_imageSourceLabel->setText("图像来源: 读取图像");
    }
}

void MainWindow::onNewProject()
{
    // 新建会丢弃当前方案：有未保存改动时先确认（与关窗/打开方案同一机制，
    // 否则只保护了"退出"这一条路，新建/打开仍是静默丢弃）
    if (!confirmDiscardUnsavedChanges(tr("新建方案")))
        return;

    // 新建方案
    logMessage("新建方案");

    // 先停掉并摘除全部执行器，再删除场景——否则运行中的执行线程会在节点被删除后
    // 继续访问（use-after-free），且 m_flowExecutors 会残留指向已删场景的悬垂键。
    // 超时则中止本次新建（N1）：保留旧方案场景与执行器，不删场景、不回收执行器，
    // 避免运行中的线程在场景被释放后访问（UAF）；旧流程被 stop 后会在算子返回自然退出。
    QList<FlowExecutor *> oldExecutors;
    for (const auto &token : m_flowExecutors) {
        FlowExecutor *ex = token ? token->executor() : nullptr;
        if (ex && !oldExecutors.contains(ex))
            oldExecutors.append(ex);
    }
    if (m_executor && !oldExecutors.contains(m_executor))
        oldExecutors.append(m_executor);

    for (FlowExecutor *ex : oldExecutors)
        ex->stopExecution();
    // 统一等待真正退出：共享 15s 总预算（与令牌收尾的阈值一致），避免 N 个执行器串行
    // wait(15000) 致 UI 最坏冻结 15s×N。超时即中止。
    const qint64 deadlineMs = QDateTime::currentMSecsSinceEpoch() + 15000;
    QList<FlowExecutor *> stuckExecutors;
    for (FlowExecutor *ex : oldExecutors) {
        if (!ex->isRunning())
            continue;
        const qint64 left = qMax(qint64(0), deadlineMs - QDateTime::currentMSecsSinceEpoch());
        if (left <= 0 || !ex->wait(left))
            stuckExecutors.append(ex);
    }
    if (!stuckExecutors.isEmpty()) {
        logMessage(tr("有流程执行线程超时未退出，已取消新建方案"));
        QMessageBox::warning(this, tr("提示"),
            tr("有流程仍在执行且未能在限定时间内退出，已取消新建方案以避免程序崩溃。"));
        return;
    }

    // 词条切片 C P3b 期：留一个占位令牌，其余令牌随表清空而释放 ⇒ 令牌析构执行收尾
    // （停止 ＋ 有界等待 ＋ 回收；超时绝不 terminate）——与原回收件同口径、同顺序。
    std::shared_ptr<FlowExecutorToken> spareToken;
    for (const auto &token : m_flowExecutors) {
        FlowExecutor *ex = token ? token->executor() : nullptr;
        if (!ex)
            continue;
        GlobalTriggerManager::instance()->unregisterExecutor(ex);
        ex->stopExecution();          // 停掉旧方案可能运行中的流程（新方案首个流程会复用 spare）
        ex->setFlowScene(nullptr);
        if (!spareToken)
            spareToken = token;   // 保留一个占位令牌，供新方案首个流程复用
    }
    m_flowExecutors.clear();
    m_executor = spareToken ? spareToken->executor() : nullptr;
    if (spareToken)
        m_flowExecutors.insert(nullptr, spareToken);

    // 清掉旧场景在图像显示控制器里的显式显示源键，避免悬垂指针（removeScene 漏 loadProjectFile 调用点）
    for (FlowScene *scene : qAsConst(m_flowScenes))
        m_imageDisplay->removeScene(scene);

    // N11 陈旧 row：结果表 + 变量缓存属于"上一方案"的数据，切方案后必须清空，
    // 否则一直显示旧流程的模块与输出、变量引用菜单给出已失效的 moduleId。
    if (m_auxPanels) {
        if (auto *rt = m_auxPanels->resultTablePanel())
            rt->clearResults();
        // 图像区「模块结果」同样属于“上一方案”的数据，必须一起清
        if (m_moduleResultPanel)
            m_moduleResultPanel->clearResults();
    }
    m_lastModuleVars.clear();

    // 清空现有的流程场景
    qDeleteAll(m_flowScenes);
    m_flowScenes.clear();
    
    // 清空标签页
    while (ui->flowTabs->count() > 0) {
        QWidget *widget = ui->flowTabs->widget(0);
        ui->flowTabs->removeTab(0);
        delete widget;
    }
    
    // 清空用户选择的输出节点（显示决策状态在 ImageDisplayController）
    m_imageDisplay->clearSelection();
    
    // 创建一个新的空流程
    createNewFlow();

    // 新建后是"干净的空白方案"：重置未保存基线。否则会拿上一方案的内容当基线，
    // 空白新方案被当成"有未保存修改"（关窗时无谓提示，自动保存还会写一份空恢复文件）。
    markProjectSaved();
    // P2-2：新方案无文件路径 → 顶栏显示「未命名方案」
    updateSchemeLabel();

    // 新建清空只读态（上一个只读分发方案已丢弃）
    projectManager()->resetReadonlyFlag();
    applyReadonlyUI();

    logMessage("新方案创建完成");
}

void MainWindow::onSaveProject()
{
    saveProjectInteractively();
}

// 保存相关逻辑集中在这里：关窗前的"保存并退出"与菜单/工具栏的保存必须走同一条路径，
// 否则很容易出现"手动保存会清恢复现场、关窗保存不会"这类隐性分叉。
ProjectManager *MainWindow::projectManager()
{
    // 懒创建集中到一处：原先两个入口各自 if(!m_projectManager) new，易漏且一旦漏就是两份实例
    // （各自的 lastFilePath 互不相同 → 自动保存/恢复拿到的是错的方案路径）。
    if (!m_projectManager) {
        m_projectManager = new ProjectManager(this);
        // U-34：载入期降级／被拒的留痕必须出现在气窗日志面板里（现场那句"标定结果不见了"
        // 得有下一句）。接线只在 ProjectLoadNotes::attachTo 那一份里写，这里只负责把出口给它。
        ProjectLoadNotes::attachTo(m_projectManager,
                                   [this](const QString &note) { logMessage(note); });
    }
    return m_projectManager;
}

bool MainWindow::saveProjectInteractively()
{
    // 保存项目（方案扩展名 .vfp，与需求文档一致）：文件对话框与结果提示在 ProjectManager
    ProjectManager *pm = projectManager();
    QString fileName;
    const bool success = pm->saveProjectInteractive(this, m_flowScenes, &fileName);
    if (fileName.isEmpty())
        return false;   // 用户取消

    logMessage(tr("保存项目: %1").arg(fileName));
    if (success) {
        logMessage(tr("项目保存成功: %1").arg(fileName));
        m_recentFiles->add(fileName);
        // 内容已落盘：刷新"未保存判定"基线，并清掉恢复现场（干净退出语义）
        markProjectSaved();
        // P2-2：顶栏方案名跟随落盘文件名
        updateSchemeLabel(fileName);
    } else {
        logMessage(tr("项目保存失败: %1").arg(fileName));
    }
    return success;
}

void MainWindow::markProjectSaved()
{
    if (!m_projectManager)
        return;
    m_recovery.markSaved(m_projectManager->buildProjectJson(m_flowScenes));
    m_recovery.clearRecovery();
}

// ---- 算子分组（FR1.9）----

FlowScene *MainWindow::currentFlowScene() const
{
    const int idx = ui && ui->flowTabs ? ui->flowTabs->currentIndex() : -1;
    if (idx >= 0 && idx < m_flowScenes.size())
        return m_flowScenes[idx];
    return nullptr;
}

void MainWindow::onCreateGroup()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    if (NodeGroupItem *group = scene->createGroupFromSelection()) {
        ui->statusBar->showMessage(tr("已创建分组「%1」（%2 个算子）")
                                       .arg(group->title())
                                       .arg(group->memberCount()),
                                   4000);
        logMessage(tr("创建分组：%1（%2 个算子）")
                       .arg(group->title())
                       .arg(group->memberCount()));
    } else {
        // 明确告诉用户为什么没反应（而不是静默什么都不发生）
        ui->statusBar->showMessage(tr("请先在画布上选中至少 2 个算子，再创建分组"), 4000);
    }
}

// ---- 运行期子流程（FR15.10）----

void MainWindow::onDefineSubFlow()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("定义为子流程"),
                                               tr("子流程名称："), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok)
        return;
    QString error;
    if (scene->defineSubFlowFromSelection(name, &error)) {
        ui->statusBar->showMessage(tr("已定义子流程「%1」（%2 个算子）。"
                                      "用「子流程」算子按此名称调用。")
                                       .arg(name.trimmed())
                                       .arg(scene->subFlowByName(name.trimmed()).members.size()),
                                   6000);
        logMessage(tr("定义子流程：%1").arg(name.trimmed()));
    } else {
        ui->statusBar->showMessage(error, 6000);
        logMessage(tr("定义子流程失败：%1").arg(error));
    }
}

void MainWindow::onRemoveSubFlow()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    const QList<SubFlowDef> defs = scene->subFlows();
    if (defs.isEmpty()) {
        ui->statusBar->showMessage(tr("当前流程没有子流程定义"), 4000);
        return;
    }
    QStringList names;
    for (const SubFlowDef &d : defs)
        names << d.name;
    bool ok = false;
    const QString name = QInputDialog::getItem(this, tr("删除子流程定义"),
                                               tr("选择要删除的子流程（画布算子会保留）："),
                                               names,
                                               0, false, &ok);
    if (!ok)
        return;
    if (scene->removeSubFlow(name)) {
        ui->statusBar->showMessage(tr("已删除子流程定义「%1」").arg(name), 4000);
        logMessage(tr("删除子流程定义：%1").arg(name));
    }
}

void MainWindow::onDissolveGroup()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    const int count = scene->dissolveSelectedGroups();
    if (count > 0) {
        ui->statusBar->showMessage(tr("已解散 %1 个分组（组内算子已保留）").arg(count), 4000);
        logMessage(tr("解散分组：%1 个").arg(count));
    } else {
        ui->statusBar->showMessage(tr("请先选中要解散的分组框（点分组标题栏）"), 4000);
    }
}

void MainWindow::onToggleGroupCollapse()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;

    QList<NodeGroupItem *> groups;
    for (QGraphicsItem *item : scene->selectedItems()) {
        if (auto *group = dynamic_cast<NodeGroupItem *>(item))
            groups.append(group);
    }
    if (groups.isEmpty()) {
        ui->statusBar->showMessage(tr("请先选中要折叠/展开的分组框（点分组标题栏）"), 4000);
        return;
    }
    for (NodeGroupItem *group : groups)
        group->setCollapsed(!group->isCollapsed());
    ui->statusBar->showMessage(tr("已折叠/展开 %1 个分组").arg(groups.size()), 3000);
    logMessage(tr("折叠/展开分组：%1 个").arg(groups.size()));
}

// ---- 子图复用：复制/粘贴（含内部连线）+ 片段文件导入导出 ----
// 边界说明：这是 FR15.10 的**设计期**半步（拷一份出来，跨流程/跨方案/跨会话搬运）；
// 运行期"把一段流程当算子调用"（子流程复用）未做，接口草案见 docs/子流程复用设计草案.md。

QPointF MainWindow::viewCenterInScene(FlowScene *scene) const
{
    if (scene && ui && ui->flowTabs) {
        for (int i = 0; i < ui->flowTabs->count(); ++i) {
            if (auto *view = qobject_cast<QGraphicsView *>(ui->flowTabs->widget(i))) {
                if (view->scene() == scene)
                    return view->mapToScene(view->viewport()->rect().center());
            }
        }
    }
    // 兜底（理论上只在没有视图的测试路径）：给一个稳妥点，别把片段粘到坐标原点外面
    return QPointF(80, 80);
}

void MainWindow::copySelectionToClipboard(FlowScene *scene)
{
    if (!scene)
        return;
    const QList<NodeBase *> selected = scene->selectedNodes();
    if (selected.isEmpty()) {
        ui->statusBar->showMessage(tr("请先选中要复制的算子"), 3000);
        return;
    }

    int droppedConnections = 0;
    const QJsonObject snippet = FlowSnippet::capture(scene, selected, &droppedConnections);
    if (snippet.isEmpty()) {
        ui->statusBar->showMessage(tr("复制失败：没有可复制的算子"), 3000);
        return;
    }
    QApplication::clipboard()->setText(FlowSnippet::toText(snippet));

    QString msg = tr("已复制 %1 个算子").arg(selected.size());
    if (droppedConnections > 0) {
        // 如实告知：跨选中边界的连线不会被带走（对端不在片段里）
        msg += tr("（%1 条跨边界的连线未包含）").arg(droppedConnections);
    }
    ui->statusBar->showMessage(msg, 5000);
    logMessage(msg);
}

void MainWindow::pasteSnippetIntoScene(FlowScene *scene)
{
    if (!scene)
        return;
    QString error;
    const QJsonObject snippet = FlowSnippet::fromText(QApplication::clipboard()->text(), &error);
    if (snippet.isEmpty()) {
        ui->statusBar->showMessage(tr("粘贴失败：%1").arg(error), 6000);
        return;
    }
    insertSnippetIntoScene(scene, snippet, &error);
}

bool MainWindow::insertSnippetIntoScene(FlowScene *scene, const QJsonObject &snippet, QString *error)
{
    if (!scene) {
        if (error)
            *error = tr("当前没有流程");
        return false;
    }
    if (scene->isEditLocked()) {
        if (error)
            *error = tr("流程处于编辑锁定状态（运行中），无法插入片段");
        ui->statusBar->showMessage(error ? *error : QString(), 5000);
        return false;
    }

    scene->recordUndo();   // 一次插入 = 一步撤销（内部批量抑制分步记录）
    const QPointF at = FlowSnippet::suggestedInsertTopLeft(snippet, viewCenterInScene(scene));
    int droppedConnections = 0;
    const QList<NodeBase *> created =
        FlowSnippet::insert(scene, snippet, at, error, &droppedConnections);
    if (created.isEmpty()) {
        if (error)
            ui->statusBar->showMessage(tr("插入片段失败：%1").arg(*error), 6000);
        if (error)
            logMessage(tr("插入片段失败：%1").arg(*error));
        return false;
    }

    // 选中刚插入的算子：用户能立刻拖走/继续连线，不用自己一个个点
    scene->clearSelection();
    for (NodeBase *n : created) {
        if (NodeGraphicsItem *item = scene->getGraphicsItemForNode(n))
            item->setSelected(true);
    }
    // 掉线必须说出来（2026-10-25 复核 P3-7）：此前只累加不报告，粘贴后"少了一根线"
    // 现场看不出来，还会以为片段本身是断的。
    const QString msg = droppedConnections > 0
        ? tr("已插入 %1 个算子；%2 条连线未能接回（端口越界或会成环）")
              .arg(created.size()).arg(droppedConnections)
        : tr("已插入 %1 个算子").arg(created.size());
    ui->statusBar->showMessage(msg, droppedConnections > 0 ? 8000 : 5000);
    logMessage(msg);
    return true;
}

void MainWindow::onExportSnippet()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    const QList<NodeBase *> selected = scene->selectedNodes();
    if (selected.isEmpty()) {
        ui->statusBar->showMessage(tr("请先选中要导出的算子"), 4000);
        return;
    }

    int droppedConnections = 0;
    const QJsonObject snippet = FlowSnippet::capture(scene, selected, &droppedConnections);
    if (snippet.isEmpty()) {
        ui->statusBar->showMessage(tr("导出失败：没有可导出的算子"), 4000);
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("导出方案片段"), QString(),
        tr("方案片段 (*%1)").arg(FlowSnippet::fileExtension()));
    if (path.isEmpty())
        return;   // 用户取消
    if (!path.endsWith(FlowSnippet::fileExtension(), Qt::CaseInsensitive))
        path += FlowSnippet::fileExtension();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("导出方案片段"),
                             tr("无法写入文件：\n%1\n%2").arg(path, file.errorString()));
        return;
    }
    const QByteArray payload = FlowSnippet::toText(snippet).toUtf8();
    const qint64 written = file.write(payload);
    file.close();
    if (written != payload.size()) {
        QMessageBox::warning(this, tr("导出方案片段"), tr("写入不完整：\n%1").arg(path));
        return;
    }

    QString msg = tr("已导出片段：%1（%2 个算子）").arg(path).arg(selected.size());
    if (droppedConnections > 0)
        msg += tr("；%1 条跨边界连线未包含").arg(droppedConnections);
    ui->statusBar->showMessage(msg, 6000);
    logMessage(msg);
}

void MainWindow::applyReadonlyUI()
{
    auto *session = SessionManager::instance();
    // 未登录时沿用动作默认可用态；已登录则以角色权限为准
    const bool roleAllowsEdit = !session->isLoggedIn() || session->canEditScheme();
    const bool readonly = projectManager()->isReadonlyLoaded();
    const bool saveEnabled = roleAllowsEdit && !readonly;
    if (ui->actionSave)
        ui->actionSave->setEnabled(saveEnabled);
    if (ui->actionSaveScheme)
        ui->actionSaveScheme->setEnabled(saveEnabled);
    if (readonly)
        ui->statusBar->showMessage(tr("只读方案：禁止覆盖保存（可另存为新方案）"), 0);
    else
        ui->statusBar->clearMessage();
}

void MainWindow::switchLanguage(I18n::Language lang)
{
    if (lang == I18n::currentLanguage())
        return; // 与当前一致，无需重启
    I18n::setLanguage(lang);
    QMessageBox::information(this, tr("语言"), tr("语言设置将在重启后生效。"));
    // Qt Widgets 大多在构造期取翻译文本，动态切换需逐个处理 LanguageChange；
    // 对工业软件最稳妥的做法是保存后重启应用，确保整棵 UI 树统一翻译。
    const QString path = QApplication::applicationFilePath();
    QStringList args = QApplication::arguments();
    if (!args.isEmpty())
        args.removeFirst(); // 去掉程序自身路径
    QProcess::startDetached(path, args);
    QApplication::quit();
}

void MainWindow::onExportEncryptedProject()
{
    if (m_flowScenes.isEmpty()) {
        QMessageBox::information(this, tr("导出加密/只读方案"),
                                 tr("当前没有可导出的方案。"));
        return;
    }

    // 小对话框：加密开关 + 口令 + 确认口令 + 只读开关
    QDialog dlg(this);
    dlg.setWindowTitle(tr("导出加密/只读方案"));
    auto *form = new QFormLayout(&dlg);
    auto *cbEncrypt = new QCheckBox(tr("加密（打开时需口令）"));
    cbEncrypt->setChecked(true);
    auto *lePass = new QLineEdit(&dlg);
    lePass->setEchoMode(QLineEdit::Password);
    auto *leConfirm = new QLineEdit(&dlg);
    leConfirm->setEchoMode(QLineEdit::Password);
    auto *cbReadonly = new QCheckBox(tr("只读（分发后禁止覆盖保存）"));
    form->addRow(tr("加密："), cbEncrypt);
    form->addRow(tr("口令："), lePass);
    form->addRow(tr("确认口令："), leConfirm);
    form->addRow(tr("只读："), cbReadonly);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString pass = lePass->text();
    const bool encrypt = cbEncrypt->isChecked();
    if (encrypt) {
        if (pass.isEmpty()) {
            QMessageBox::warning(this, tr("导出加密/只读方案"), tr("加密必须设置口令。"));
            return;
        }
        if (pass != leConfirm->text()) {
            QMessageBox::warning(this, tr("导出加密/只读方案"), tr("两次输入的口令不一致。"));
            return;
        }
    }
    const bool readonly = cbReadonly->isChecked();

    QString path = QFileDialog::getSaveFileName(
        this, tr("导出加密/只读方案"), QString(),
        tr("加密方案 (*.vfpe);;方案文件 (*.vfp)"));
    if (path.isEmpty())
        return; // 用户取消
    if (!path.endsWith(QStringLiteral(".vfpe"), Qt::CaseInsensitive) &&
        !path.endsWith(QStringLiteral(".vfp"), Qt::CaseInsensitive))
        path += QStringLiteral(".vfpe");

    ProjectManager *pm = projectManager();
    const bool ok = pm->exportEncryptedProject(path, m_flowScenes,
                                              encrypt ? pass : QString(), readonly);
    if (ok) {
        const QString tag = (encrypt ? tr("加密") : tr("未加密")) +
                            (readonly ? tr("+只读") : QString());
        const QString msg = tr("已导出%1方案：%2").arg(tag, path);
        ui->statusBar->showMessage(msg, 8000);
        logMessage(msg);
    } else {
        QMessageBox::warning(this, tr("导出加密/只读方案"),
                             tr("导出失败：\n%1").arg(path));
    }
}

void MainWindow::onImportSnippet()
{
    FlowScene *scene = currentFlowScene();
    if (!scene)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("导入方案片段"), QString(),
        tr("方案片段 (*%1)").arg(FlowSnippet::fileExtension()));
    if (path.isEmpty())
        return;   // 用户取消

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("导入方案片段"),
                             tr("无法读取文件：\n%1\n%2").arg(path, file.errorString()));
        return;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QString error;
    const QJsonObject snippet = FlowSnippet::fromText(QString::fromUtf8(raw), &error);
    if (snippet.isEmpty()) {
        QMessageBox::warning(this, tr("导入方案片段"), error);
        return;
    }
    logMessage(tr("导入片段：%1").arg(path));
    if (!insertSnippetIntoScene(scene, snippet, &error))
        QMessageBox::warning(this, tr("导入方案片段"), error);
}

void MainWindow::initCrashRecovery()
{
    // 间隔可配：现场无人值守机想省 I/O 就调大，置 0 则关掉自动保存
    // （关掉后仍保留"未保存改动"的关窗确认，不会变成静默丢弃）。
    QSettings settings;
    const int intervalSec =
        settings.value(QStringLiteral("recovery/autoSaveIntervalSec"), 60).toInt();
    if (intervalSec <= 0) {
        VFP_DEBUG << "自动保存已按配置关闭（recovery/autoSaveIntervalSec <= 0）";
        return;
    }

    if (!m_autoSaveTimer) {
        m_autoSaveTimer = new QTimer(this);
        connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSaveTick);
    }
    m_autoSaveTimer->start(intervalSec * 1000);
    VFP_DEBUG << "自动保存已启用：间隔" << intervalSec << "秒，目录" << m_recovery.dirPath();
}

void MainWindow::autoSaveTick()
{
    // 同步执行/重算独占 UI 线程：此刻序列化既慢又没意义，跳过一拍即可
    if (m_busyExecuting)
        return;

    ProjectManager *pm = projectManager();
    const QJsonObject json = pm->buildProjectJson(m_flowScenes);

    // 与"已落盘内容"一致（例如此前刚保存过）：不写恢复文件。
    // 否则干净退出后还会留下一个"可恢复"的假现场——下次启动弹一个恢复框而内容其实没变。
    if (!m_recovery.hasUnsavedChanges(json))
        return;
    // 同一份内容已写过（用户改了又停手）：不重复写盘，避免每分钟一次无意义 I/O 与状态栏刷屏
    if (m_recovery.recoveryUpToDate(json))
        return;

    if (m_recovery.saveRecovery(json, pm->lastFilePath()))
        ui->statusBar->showMessage(tr("已自动保存（异常退出后可恢复）"), 3000);
}

void MainWindow::initReportAutoExport()
{
    // 定时导出（P1-11 收尾）：策略与落盘在 ReportAutoExport（无 GUI、可单测），
    // 这里只负责"每分钟看一眼"与取数。定时器**总是启动**：用户可能在报表窗口里随时开启，
    // 若只在"启动时已启用"才启定时器，就会出现"勾了开关却不生效、要重启"的怪事。
    if (!m_reportTimer) {
        m_reportTimer = new QTimer(this);
        connect(m_reportTimer, &QTimer::timeout, this, &MainWindow::reportAutoExportTick);
    }
    m_reportTimer->start(60 * 1000);

    const ReportAutoExport::Config cfg = ReportAutoExport::loadConfig();
    if (cfg.enabled && cfg.isValid()) {
        VFP_DEBUG << "定时导出已启用：每" << cfg.intervalMinutes << "分钟，统计最近"
                  << cfg.rangeHours << "小时，目录" << cfg.effectiveDir();
    } else {
        VFP_DEBUG << "定时导出未启用（可在「视图 → 统计报表」里开启）";
    }
}

void MainWindow::reportAutoExportTick()
{
    ReportAutoExport::Config cfg = ReportAutoExport::loadConfig();
    if (!cfg.enabled || !cfg.isValid())
        return;   // 未启用：静默（不打扰；开关在报表窗口里）

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!ReportAutoExport::shouldExport(cfg, now, ReportAutoExport::lastExportMs()))
        return;

    // 取数：与报表窗口**同一个查询接口、同一套计算**。口径必须一致，
    // 否则"界面看到 98%、机器导出来 96%"这种事现场无法解释。
    const QDateTime to = QDateTime::currentDateTime();
    const QDateTime from = to.addSecs(-3600LL * cfg.rangeHours);
    // 上限保护：本拍在 GUI 线程同步查询并逐行构造记录，行数直接换算成窗口冻结时长
    //（评审 P3：高频产线"每天一次、没人看着"的时刻最忌讳卡窗）。保守钉 5 万（原 50 万的
    // 1/10，具体耗时待真机标定）——queryResults 按时间倒序取最近 N 条，
    // 触顶时报表只覆盖"最近 5 万条"，状态栏如实提示。
    constexpr int kQueryLimit = 50000;
    const QList<InspectionRecord> records =
        AppDatabase::instance()->queryResults(from, to, kQueryLimit);
    const bool truncated = (records.size() >= kQueryLimit);

    // 粒度与报表窗口"自动"模式保持一致：统计窗口 ≤ 2 天（48 小时）按小时，更长按天。
    // 否则会出现"界面看的是按天、机器定时导出的是按小时"这类口径不一致，现场无法解释。
    const StatisticsReport::Granularity gran =
        (cfg.rangeHours <= 48) ? StatisticsReport::Granularity::ByHour
                               : StatisticsReport::Granularity::ByDay;
    const StatisticsReport::Summary summary =
        StatisticsReport::compute(records, QString(), gran);
    const double target =
        QSettings().value(QStringLiteral("reporting/yieldTargetPercent"), 0.0).toDouble();

    QStringList written;
    QString error;
    if (!ReportAutoExport::exportNow(cfg, summary, QString(), target, to, &written, &error)) {
        // 失败**不更新** lastExportMs：下一分钟自动重试（目录临时不可写这类问题能自愈）
        logMessage(tr("定时导出失败：%1").arg(error));
        ui->statusBar->showMessage(tr("定时导出失败：%1").arg(error), 15000);
        return;
    }

    ReportAutoExport::setLastExportMs(now);
    const int removed = ReportAutoExport::pruneOldFiles(cfg.effectiveDir(), cfg.keepFiles);

    QString msg = tr("已定时导出 %1 个文件到 %2").arg(written.size()).arg(cfg.effectiveDir());
    if (removed > 0)
        msg += tr("（清理旧报告 %1 个）").arg(removed);
    if (truncated)
        msg += tr(" ⚠ 记录数触顶（仅覆盖最近 %1 条），报表可能不完整（建议调小统计小时数）").arg(kQueryLimit);
    logMessage(msg);
    ui->statusBar->showMessage(msg, 12000);
    if (!error.isEmpty())
        logMessage(tr("定时导出部分失败：%1").arg(error));
}

void MainWindow::checkRecoveryOnStartup()
{
    const RecoveryStore::Info info = m_recovery.info();
    if (!info.exists)
        return;

    const QString filePath =
        QDir(m_recovery.dirPath()).filePath(RecoveryStore::recoveryFileName());

    if (!info.readable) {
        // 损坏的恢复文件：明说并清掉，否则每次启动都弹同一个无法处理的框
        VFP_DEBUG << "恢复文件损坏，已清除：" << filePath;
        m_recovery.clearRecovery();
        logMessage(tr("上次异常退出留下的恢复文件已损坏，无法恢复，已清除"));
        return;
    }

    QSettings settings;
    if (!settings.value(QStringLiteral("recovery/promptOnStartup"), true).toBool())
        return;   // 现场可关掉启动询问；此时保留文件，不静默丢弃内容

    const QString when = info.savedAt.isValid()
                             ? info.savedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                             : tr("时间未知");
    const QString origin = info.originalPath.isEmpty()
                               ? tr("未命名方案（从未保存过）")
                               : info.originalPath;

    QMessageBox box(this);
    box.setWindowTitle(tr("崩溃恢复"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("检测到上次运行未正常退出，存在自动保存的内容："));
    box.setInformativeText(tr("原方案：%1\n自动保存时间：%2\n\n是否恢复到界面？\n"
                             "（选择「丢弃」将删除该恢复文件）")
                               .arg(origin, when));
    QPushButton *restoreBtn = box.addButton(tr("恢复"), QMessageBox::AcceptRole);
    box.addButton(tr("丢弃"), QMessageBox::DestructiveRole);
    box.setDefaultButton(restoreBtn);
    box.exec();

    if (box.clickedButton() != restoreBtn) {
        m_recovery.clearRecovery();
        logMessage(tr("已丢弃上次异常退出留下的自动保存内容"));
        return;
    }

    QString restorePath;
    if (!m_recovery.exportForLoad(&restorePath)) {
        QMessageBox::warning(this, tr("崩溃恢复"),
                             tr("恢复失败：无法读取恢复文件。\n%1").arg(filePath));
        return;
    }

    // 复用唯一的"接管方案"路径：停旧执行器、建标签页、注册触发、边界收尾都在 loadProjectFile 内。
    // 临时导出文件删掉即可（内容已在场景里）；恢复出来的方案不属于该临时文件，
    // 故把 lastFilePath 写回原方案路径（空 = 从未保存过的新方案）。
    loadProjectFile(restorePath, /*recoveryRestore=*/true);
    QFile::remove(restorePath);
    projectManager()->setLastFilePath(info.originalPath);

    logMessage(tr("已从自动保存内容恢复（原方案：%1；自动保存时间：%2）。"
                  "请确认后用「保存」写回方案文件")
                   .arg(origin, when));
}

bool MainWindow::confirmDiscardUnsavedChanges(const QString &action)
{
    QSettings settings;
    // 与关窗提示同一个开关：无人值守现场可整体关掉"未保存"询问
    if (!settings.value(QStringLiteral("recovery/promptOnClose"), true).toBool())
        return true;

    ProjectManager *pm = projectManager();
    const QJsonObject json = pm->buildProjectJson(m_flowScenes);
    if (!m_recovery.hasUnsavedChanges(json))
        return true;

    QMessageBox box(this);
    box.setWindowTitle(tr("未保存的修改"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("%1 前，当前方案有未保存的修改：").arg(action));
    box.setInformativeText(
        tr("方案：%1\n\n「保存并继续」写回方案文件；「不保存继续」将丢弃这些修改。")
            .arg(pm->lastFilePath().isEmpty() ? tr("未命名方案（从未保存过）")
                                              : pm->lastFilePath()));
    QPushButton *saveBtn = box.addButton(tr("保存并继续"), QMessageBox::AcceptRole);
    QPushButton *discardBtn = box.addButton(tr("不保存继续"), QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    box.exec();

    if (box.clickedButton() == cancelBtn)
        return false;
    if (box.clickedButton() == discardBtn) {
        // 当前方案即将被替换/丢弃：恢复现场随之作废（不留一份"已被用户放弃"的方案）
        m_recovery.clearRecovery();
        return true;
    }
    return saveProjectInteractively();   // 保存失败或用户在保存对话框取消 → 不继续
}

bool MainWindow::confirmCloseWithUnsavedChanges()
{
    QSettings settings;
    if (!settings.value(QStringLiteral("recovery/promptOnClose"), true).toBool())
        return true;   // 不询问：保留恢复文件，交给下次启动询问（不在这里静默丢弃用户改动）

    ProjectManager *pm = projectManager();
    const QJsonObject json = pm->buildProjectJson(m_flowScenes);
    if (!m_recovery.hasUnsavedChanges(json)) {
        m_recovery.clearRecovery();   // 干净退出：不留恢复现场
        return true;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("退出前确认"));
    box.setIcon(QMessageBox::Warning);
    box.setText(tr("方案有未保存的修改："));
    box.setInformativeText(
        tr("方案：%1\n\n「保存并退出」写回方案文件；「不保存退出」将丢弃这些修改，"
           "自动保存内容也会一并清除。")
            .arg(pm->lastFilePath().isEmpty() ? tr("未命名方案（从未保存过）")
                                              : pm->lastFilePath()));
    QPushButton *saveBtn = box.addButton(tr("保存并退出"), QMessageBox::AcceptRole);
    QPushButton *discardBtn = box.addButton(tr("不保存退出"), QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(tr("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    box.exec();

    if (box.clickedButton() == cancelBtn)
        return false;
    if (box.clickedButton() == discardBtn) {
        VFP_DEBUG << "用户选择不保存退出，清除恢复现场";
        m_recovery.clearRecovery();
        return true;
    }

    // 保存并退出：保存失败或用户在保存对话框里取消都不关窗（否则等于悄悄丢修改）
    return saveProjectInteractively();
}

void MainWindow::onExit()
{
    // 退出
    close();
}

void MainWindow::onManageGlobalCameras()
{
    // 管理全局相机（非模态：开着相机管理也能继续操作主界面）
    showAuxDialog(QStringLiteral("globalCameras"), [this]() -> QDialog * {
        auto *dlg = new GlobalCameraDialog(this);
        connect(dlg, &GlobalCameraDialog::logMessage, this, &MainWindow::logMessage);
        return dlg;
    });
}



void MainWindow::onStartExecution()
{
    if (!m_executor)
        return;
    // 运行控制与流程模式解耦（对齐 VisionMaster 的操作逻辑）：任何模式下都能"开始"，暂停中按=继续。
    // 旧实现写死"仅软触发有效"，连续/硬触发模式下点它毫无反应（静默忽略、无任何提示），而这两种模式
    // 此前又靠"切模式自动开跑" → 用户停止后既找不到启动入口、点开始也没反应（现场反馈"点了没用"）。
    // 判定核在 ExecutionControlPolicy（切片 B 首抽）：这里是表现位——执行器调用与文案全留窗侧。
    switch (ExecutionControlPolicy::startIntent(m_executor->getState(),
                                                SessionManager::instance()->canRunFlow())) {
    case ExecutionControlPolicy::Intent::Resume:
        m_executor->resumeExecution();   // 已在跑/暂停：按"开始"视为继续，不留"点了没反应"的死按钮
        return;
    case ExecutionControlPolicy::Intent::GateClosedRefuse:
        // 173 行拍板：闸开不发新跑——运行态的继续走上面分支，新一次发起在这里收闸。
        logMessage(tr("写闸开启期间不受理新的运行：出厂默认口令仍在使用"
                      "（正在跑的流程不受影响，改密后恢复）"));
        return;
    case ExecutionControlPolicy::Intent::StartNewRun:
    default:
        break;
    }
    // 明确点"开始执行"= 正常跑一遍，退出上次遗留的单步模式
    // （历史缺陷：单步模式粘住，之后每次"开始执行"仍在每个节点后暂停）
    m_executor->exitStepMode();
    // P2-8：每轮开始前解析参数订阅（全局变量 / 上游模块输出）
    applyParamSubscriptions(currentFlowScene());
    m_executor->startExecution();
}

void MainWindow::onPauseResumeExecution()
{
    if (!m_executor)
        return;
    switch (ExecutionControlPolicy::pauseResumeIntent(m_executor->getState())) {
    case ExecutionControlPolicy::Intent::Pause:
        m_executor->pauseExecution();
        return;
    case ExecutionControlPolicy::Intent::Resume:
        m_executor->resumeExecution();
        return;
    default:
        break;   // 其余状态按钮为禁用态，不会到达这里
    }
}

void MainWindow::onSingleShotExecution()
{
    // 单次执行：仅在软触发模式可用（按钮在连续/硬触发模式下已禁用），运行一次当前流程。
    // 判定序与原槽直落序同向（Running → 非软触发 → 闸位），求值序取逐位局部量先落。
    const ExecutionState st = m_executor->getState();
    const FlowMode mode = m_executor->getFlowMode();
    const bool canRun = SessionManager::instance()->canRunFlow();
    switch (ExecutionControlPolicy::singleShotIntent(st, mode, canRun)) {
    case ExecutionControlPolicy::Intent::GateClosedRefuse:
        // 173 行拍板：闸开不发新跑（单次执行也是一次新发起）
        logMessage(tr("写闸开启期间不受理单次执行：出厂默认口令仍在使用（改密后恢复）"));
        return;
    case ExecutionControlPolicy::Intent::StartOnce:
        // P2-8：单次执行同样先解析参数订阅
        applyParamSubscriptions(currentFlowScene());
        m_executor->startExecution();
        return;
    case ExecutionControlPolicy::Intent::None:
    default:
        return;
    }
}

void MainWindow::onStopExecution()
{
    // 停止执行
    m_executor->stopExecution();
}

void MainWindow::onLoadProject()
{
    // 加载项目：路径选择在 ProjectManager（取消返回空串）
    const QString fileName = ProjectManager::askOpenProjectPath(this);
    if (!fileName.isEmpty()) {
        loadProjectFile(fileName);
    }
}

void MainWindow::loadProjectFile(const QString &fileName, bool recoveryRestore)
{
    // 崩溃恢复也走这条路径（复用唯一的"接管方案"实现），但它不算一次"打开方案"：
    // 不写"加载项目"日志、不记入最近文件、不把内容标为已落盘（详见函数末尾）。
    if (!recoveryRestore) {
        // 打开方案会替换当前方案：有未保存改动时先确认（该检查覆盖"最近打开"等所有入口）
        if (!confirmDiscardUnsavedChanges(tr("打开方案")))
            return;
        logMessage(tr("加载项目: %1").arg(fileName));
    }

    // 使用ProjectManager加载项目
    ProjectManager *pm = projectManager();

    // 加密信封：打开前若需口令则弹窗索取；取消则放弃本次打开（保持当前方案）。
    QString passphrase;
    if (ProjectManager::fileNeedsPassphrase(fileName)) {
        bool ok = false;
        passphrase = QInputDialog::getText(this, tr("打开加密方案"),
            tr("该方案已加密，请输入口令："), QLineEdit::Password, QString(), &ok);
        if (!ok)
            return; // 用户取消
    }

    // U-201 载入门禁：方案目录外路径须显式确认（模态，逐条列路径；U-215 含相对逃逸形态）；
    // 未确认即拒载——当前方案保持不变，窗口侧不接管任何半装载内容。
    pm->setOutsidePathConfirmer([this](const ProjectPaths::PathReview &review) {
        return confirmOutsideSchemePaths(review);
    });

    // 先加载到临时列表：只有加载成功才销毁当前方案。
    // 原实现是先 qDeleteAll 再加载，文件损坏时旧方案已丢、新方案为空，且仍报“加载成功”。
    QList<FlowScene*> loadedScenes;
    const bool success = pm->loadProject(fileName, loadedScenes, passphrase);

    if (!success) {
        qDeleteAll(loadedScenes);
        if (pm->lastLoadRefusedForOutsidePaths())
            logMessage(tr("项目未载入：方案含方案目录外路径（绝对或相对逃逸）且未获确认（当前方案保持不变）"));
        else if (pm->lastLoadRefusedForIntegrity())
            logMessage(tr("项目未载入：旧明文打包格式（不带完整性校验位）本版本不再接收（当前方案保持不变）"));
        else if (ProjectManager::fileNeedsPassphrase(fileName))
            logMessage(tr("项目加载失败：口令错误或文件损坏（当前方案保持不变）"));
        else
            logMessage(tr("项目加载失败：文件不存在、格式损坏或内容为空（当前方案保持不变）"));
        return;
    }
    applyReadonlyUI();

    // 176 行（旧格式加注）：v1 明文打包与裸 JSON 成功载入后提示一次，不阻断、不逐项唠叨。
    if (!recoveryRestore) {
        const QString integrityNote = pm->lastIntegrityNote();
        if (!integrityNote.isEmpty())
            logMessage(tr("提示：该方案为%1（无完整性校验位），建议用「另存为」重新导出一次获得校验位")
                           .arg(integrityNote));
    }

    // 释放旧方案的执行器：先停全部并等待真正退出；超时则中止本次打开（N1）——保留旧方案
    // 场景与执行器，不删场景、不回收执行器。运行中的线程若此刻删场景 = UAF，比崩溃更该避免的是
    // 脏数据；这里直接中止切换，旧流程被 stop 后会在算子返回自然退出，方案保持原样可再用。
    QList<FlowExecutor *> oldExecutors;
    for (const auto &token : m_flowExecutors) {
        FlowExecutor *ex = token ? token->executor() : nullptr;
        if (ex && !oldExecutors.contains(ex))
            oldExecutors.append(ex);
    }
    if (m_executor && !oldExecutors.contains(m_executor))
        oldExecutors.append(m_executor);

    for (FlowExecutor *ex : oldExecutors)
        ex->stopExecution();
    // 统一等待真正退出：共享 15s 总预算（与令牌收尾的阈值一致），避免 N 个执行器串行
    // wait(15000) 致 UI 最坏冻结 15s×N。超时即中止。
    const qint64 deadlineMs = QDateTime::currentMSecsSinceEpoch() + 15000;
    QList<FlowExecutor *> stuckExecutors;
    for (FlowExecutor *ex : oldExecutors) {
        if (!ex->isRunning())
            continue;
        const qint64 left = qMax(qint64(0), deadlineMs - QDateTime::currentMSecsSinceEpoch());
        if (left <= 0 || !ex->wait(left))
            stuckExecutors.append(ex);
    }
    if (!stuckExecutors.isEmpty()) {
        // 超时分支联动中止：不删场景、不回收执行器，旧方案保持完整（含仍运行的线程，其场景未删）。
        // 顺手释放本次已加载但未启用的新场景（loadedScenes 仍持有它们），否则泄漏。
        qDeleteAll(loadedScenes);
        loadedScenes.clear();
        logMessage(tr("有流程执行线程超时未退出，已取消打开方案"));
        QMessageBox::warning(this, tr("提示"),
            tr("有流程仍在执行且未能在限定时间内退出，已取消打开方案以避免程序崩溃。"));
        return;
    }

    // 词条切片 C P3b 期：留一个占位令牌，其余令牌随表清空而释放 ⇒ 令牌析构执行收尾
    // （停止 ＋ 有界等待 ＋ 回收；超时绝不 terminate）——与原回收件同口径、同顺序。
    std::shared_ptr<FlowExecutorToken> spareToken;
    for (const auto &token : m_flowExecutors) {
        FlowExecutor *ex = token ? token->executor() : nullptr;
        if (!ex)
            continue;
        GlobalTriggerManager::instance()->unregisterExecutor(ex);
        ex->stopExecution();
        ex->setFlowScene(nullptr);
        if (!spareToken)
            spareToken = token;   // 保留一个占位令牌，供新方案首个流程复用
    }
    m_flowExecutors.clear();
    m_executor = spareToken ? spareToken->executor() : nullptr;
    if (spareToken)
        m_flowExecutors.insert(nullptr, spareToken);

    // 清掉旧场景在图像显示控制器里的显式显示源键，避免悬垂指针（removeScene 漏 loadProjectFile 调用点）
    for (FlowScene *scene : qAsConst(m_flowScenes))
        m_imageDisplay->removeScene(scene);

    // N11 陈旧 row：结果表 + 变量缓存属于"上一方案"的数据，切方案后必须清空，
    // 否则一直显示旧流程的模块与输出、变量引用菜单给出已失效的 moduleId。
    if (m_auxPanels) {
        if (auto *rt = m_auxPanels->resultTablePanel())
            rt->clearResults();
        // 图像区「模块结果」同样属于“上一方案”的数据，必须一起清
        if (m_moduleResultPanel)
            m_moduleResultPanel->clearResults();
    }
    m_lastModuleVars.clear();

    // 清空现有的流程场景
    qDeleteAll(m_flowScenes);
    m_flowScenes.clear();

    // 清空标签页
    while (ui->flowTabs->count() > 0) {
        QWidget *widget = ui->flowTabs->widget(0);
        ui->flowTabs->removeTab(0);
        delete widget;
    }

    logMessage(tr("项目加载成功"));
    // VM 5.2.5 响应配置：方案加载成功 → 通知外部通信设备（配置含延迟时走定时器）
    CommResponseManager::instance()->notifySchemeLoaded();
    // P2-2：顶栏方案名跟随载入文件名（崩溃恢复也显示来源方案名）
    updateSchemeLabel(fileName);
    if (!recoveryRestore)
        m_recentFiles->add(fileName);

    // 添加加载的场景到标签页，并逐个注册到全局触发管理器。
    // 修复：此前只有 createNewFlow 才做 flowName/registerFlow，载入方案的所有流程在 GTM 里
    // 无名无映射 → 硬触发/外部触发全灭（触发链路静默失效）。
    m_flowModes.clear();   // S5：载入前清空旧流程模式映射，避免已删场景的 stale 条目被复用继承

    for (int i = 0; i < loadedScenes.size(); ++i) {
        FlowScene *scene = loadedScenes[i];
        m_flowScenes.append(scene);

        // S2：优先用方案持久化的流程名（保持触发绑定身份），缺失时再按全局唯一名分配，
        // 避免"按页签序号重建名"把触发绑定平移到别的流程（比空路由更危险的错触发）。
        QString flowName = scene->flowName();
        if (flowName.isEmpty())
            flowName = GlobalTriggerManager::instance()->allocFlowName();
        scene->setFlowName(flowName);   // 回写，保证重新保存时身份一致

        // 每流程运行模式：优先用方案里存的（不是所有流程都要连续），缺失/越界再回落软触发；
        // 同时写回 m_flowModes（组合框/按钮态都读它）并同步给该流程的执行器。
        const int persistedMode = scene->flowMode();
        const FlowMode flowMode = (persistedMode >= 0 && persistedMode <= 2)
                                      ? static_cast<FlowMode>(persistedMode)
                                      : FlowMode::SoftwareTrigger;
        m_flowModes[scene] = flowMode;
        scene->setFlowMode(static_cast<int>(flowMode));   // F-2：越界脏值（如手工改文件写入 7）在此规范化为 0/1/2，
                                                           // 否则下次保存会把脏值原样写回、永远靠下游兜底
        const QString modeSuffix = flowModeSuffix(flowMode);   // F-3：后缀映射单一来源（越界行为已统一）

        FlowCanvasView *view = new FlowCanvasView(scene);
        VisionWorkbenchStyle::applyGraphicsViewWorkbenchDefaults(view);
        view->setDragMode(QGraphicsView::RubberBandDrag);
        view->setRubberBandSelectionMode(Qt::IntersectsItemShape);
        view->setFocusPolicy(Qt::StrongFocus);
        // P2：状态栏缩放百分比跟随当前流程画布
        connect(view, &FlowCanvasView::zoomPercentChanged, this, [this, view](int percent) {
            if (m_zoomStatusLabel && view == canvasViewForScene(currentFlowScene()))
                m_zoomStatusLabel->setText(tr("缩放 %1%").arg(percent));
        });
        ui->flowTabs->addTab(view, flowName + modeSuffix);   // 显示名与路由名一致，并标出该流程模式

        // 连接信号
        hookFlowScene(scene);

        // 每个流程独立执行器 + 流程名注册（多流程并发：executorForScene 复用占位 spare 给首个
        // 流程、其余各自新建；与 createNewFlow 同一套 setFlowName/registerFlow，保证触发按名路由）。
        FlowExecutor *flowEx = executorForScene(scene);
        if (flowEx) {
            flowEx->setFlowScene(scene);
            flowEx->setFlowName(flowName);
            flowEx->setFlowMode(flowMode);   // 载入即按方案里的模式（连续/软触发/硬触发）生效
            GlobalTriggerManager::instance()->registerFlow(flowName, scene,
                                                           m_flowExecutors.value(scene));
        }
    }

    // 载入方案后按"每流程模式"自启动（现场选 B：无人值守产线要连续流程自动开跑）。
    // 规则：只对**连续**模式自动开始；软触发/硬触发仍由操作员显式点「开始执行」
    // （硬触发是"点开始进入等待"，自动进入等待会让流程在无人确认时等待触发，故不自动）。
    // 延到事件循环下一拍执行：让载入/建标签页/注册执行器先收尾，避免在载入过程中就起线程跑图。
    QMetaObject::invokeMethod(this, [this]() {
        for (FlowScene *scene : m_flowScenes) {
            if (!scene)
                continue;
            const auto it = m_flowModes.constFind(scene);
            if (it == m_flowModes.cend() || it.value() != FlowMode::Continuous)
                continue;
            FlowExecutor *ex = executorForScene(scene);
            if (!ex)
                continue;
            const ExecutionState st = ex->getState();
            if (st == ExecutionState::Running || st == ExecutionState::Paused)
                continue;   // 已在运行的流程不重复启动
            // 173 行拍板：闸开不发新跑——载入自动连续跑属新一次发起，随闸收；
            // 改密解除后从这里（或「开始执行」）即可恢复自动运行。
            if (!SessionManager::instance()->canRunFlow()) {
                logMessage(tr("写闸开启期间不自动开始运行：「%1」为连续模式但出厂口令未改"
                              "（改密后可从「开始执行」恢复）")
                               .arg(scene->flowName().isEmpty() ? QStringLiteral("未命名流程")
                                                                : scene->flowName()));
                continue;
            }
            ex->startExecution();
            logMessage(QStringLiteral("载入方案：「%1」为连续模式，已自动开始运行（可用「暂停/停止」控制）")
                           .arg(scene->flowName().isEmpty() ? QStringLiteral("未命名流程")
                                                            : scene->flowName()));
        }
    }, Qt::QueuedConnection);

    // 设置当前场景（多流程并发：切换到该流程的执行器）
    if (!m_flowScenes.isEmpty()) {
        m_executor = executorForScene(m_flowScenes[0]);
        ui->flowTabs->setCurrentIndex(0);
    }
    checkHalconNodesHint();

    // 打开方案默认回到设计态：工具箱/图像显示/参数/日志四个面板必须可见。
    // 现场反馈：上次停在运行态时打开方案，画布铺满、左侧工具箱和右侧图像都不见了。
    if (!recoveryRestore) {
        switchToDesignMode();
        if (ui->imageDock && ui->imageDock->isFloating())
            onToggleImageFloat();       // 从运行态最大化浮窗还原为右侧停靠
        if (ui->toolDock) {
            ui->toolDock->show();
            ui->toolDock->raise();
        }
        if (ui->imageDock) {
            ui->imageDock->show();
            ui->imageDock->raise();
        }
        // 旧存档可能把左侧工具箱拖成 0 宽（"看得见面板但点不到/看不见"），
        // 打开方案时给一个可用的默认宽度；收起态只保证图标竖条可见。
        if (ui->toolDock && !ui->toolDock->isFloating()) {
            const int wantWidth = m_toolBrowserExpanded ? 220 : 48;
            if (ui->toolDock->width() < wantWidth)
                resizeDocks({ui->toolDock}, {wantWidth}, Qt::Horizontal);
        }
        if (ui->imageDock && !ui->imageDock->isFloating() && ui->imageDock->width() < 220)
            resizeDocks({ui->imageDock}, {qMax(360, width() / 3)}, Qt::Horizontal);
    }

    // 记录"当前内容已落盘"基线（内容此时与刚读入的文件一致）。
    // 崩溃恢复例外：恢复出来的内容还没写回原方案文件，必须保持"未保存"状态，
    // 否则关窗时不会提示保存，用户会以为已经存好了——丢工作的经典路径。
    if (!recoveryRestore)
        markProjectSaved();
}

bool MainWindow::confirmOutsideSchemePaths(const ProjectPaths::PathReview &review)
{
    // U-201 载入门禁：逐条点名方案目录外路径（节点 · 参数 · 原路径；
    // U-215 起含相对逃逸形态——解析后落出方案目录的相对 `..` 与目录外绝对同权）。
    // 默认按钮落在「否」上——误按回车即拒载（拒载无副作用：当前方案保持不变）。
    const QVector<ProjectPaths::PathReviewHit> hits =
        review.outsideHits + review.escapedRelativeHits;
    const int total = hits.size();
    const int shown = qMin(10, total);
    QStringList lines;
    for (int i = 0; i < shown; ++i) {
        const ProjectPaths::PathReviewHit &hit = hits.at(i);
        lines << QStringLiteral("%1 · %2：%3").arg(hit.nodeName, hit.paramName, hit.rawPath);
    }
    if (total > shown) {
        lines << QStringLiteral("……另有 %1 处未逐条列出").arg(total - shown);
    }
    QMessageBox box(QMessageBox::Warning, tr("方案含方案目录外路径"),
                    tr("该方案引用了 %1 处方案目录外路径（绝对或相对逃逸）：\n\n%2\n\n"
                       "是否确认放行载入？不确认则本次载入取消，当前方案保持不变。")
                        .arg(total)
                        .arg(lines.join(QStringLiteral("\n"))),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    QStringList allLines;
    for (const ProjectPaths::PathReviewHit &hit : hits) {
        allLines << QStringLiteral("%1 · %2：%3").arg(hit.nodeName, hit.paramName, hit.rawPath);
    }
    box.setDetailedText(allLines.join(QStringLiteral("\n")));
    return box.exec() == QMessageBox::Yes;
}

void MainWindow::checkHalconNodesHint()
{
    QSettings settings;
    if (settings.value(QStringLiteral("skipHalconNodeHint"), false).toBool()) {
        return;
    }

    QStringList names;
    for (FlowScene *scene : m_flowScenes) {
        const QList<NodeBase *> nodes = scene->nodes();
        for (NodeBase *node : nodes) {
            if (qobject_cast<DeepOcrNode *>(node)
                || qobject_cast<HalconImageSourceNode *>(node)) {
                if (names.size() < 8)
                    names << node->name();
            }
        }
    }
    if (names.isEmpty())
        return;

    QMessageBox box(QMessageBox::Information,
                    QStringLiteral("HALCON 运行时"),
                    QStringLiteral("本方案使用 HALCON 运行时：%1。\n\n"
                                   "日常算法（二值化、匹配、卡尺、缺陷等）走 OpenCV，"
                                   "不依赖 HALCON 区域/测量。\n"
                                   "请确认本机 HALCON 可用于读图与 DeepOCR。")
                        .arg(names.join(QStringLiteral("、"))),
                    QMessageBox::Ok);
    QCheckBox *rememberBox = new QCheckBox(QStringLiteral("不再提示"));
    box.setCheckBox(rememberBox);
    box.exec();
    if (rememberBox->isChecked()) {
        settings.setValue(QStringLiteral("skipHalconNodeHint"), true);
    }
}

void MainWindow::runHalconEnvCheck()
{
    QString detail;
    const HalconEnvStatus status = halconEnvironmentCheck(&detail);
    const QString title = (status == HalconEnvStatus::Ok)
                              ? QStringLiteral("HALCON 图像层可用")
                              : QStringLiteral("HALCON 图像层不可用");
    QMessageBox box(status == HalconEnvStatus::Ok ? QMessageBox::Information : QMessageBox::Warning,
                    title, detail, QMessageBox::Ok, this);
    box.exec();
}

void MainWindow::onAddFlowTab()
{
    // 添加流程标签页
    createNewFlow();
}

void MainWindow::onCloseFlowTab(int index)
{
    if (index < 0 || index >= m_flowScenes.size() || index >= ui->flowTabs->count())
        return;

    // P0-1：关闭标签页 X / Ctrl+Shift+T / 流程栏右键删除统一走确认，且删除前留快照。
    FlowScene *scene = m_flowScenes[index];
    if (!confirmDeleteFlow(scene, index))
        return;

    // 保存指针
    QWidget *tabWidget = ui->flowTabs->widget(index);
    // 该流程若正被超时监控，先解除（场景随后会被删除）
    if (m_timeoutScene == scene) {
        m_timeoutScene = nullptr;
        if (m_flowTimeoutTimer)
            m_flowTimeoutTimer->stop();
    }
    m_flowModes.remove(scene);
    m_imageDisplay->removeScene(scene);   // 清掉已删场景的显式显示源键，避免悬垂指针
    // 如果执行器正运行此场景，先停止；未确认退出前不得继续（后面会删除场景，
    // 执行线程仍在跑就是 use-after-free）。历史实现只等 1 秒且不检查返回值。
    FlowExecutor *sceneEx = executorForScene(scene);
    if (sceneEx) {
        sceneEx->stopExecution();
        if (sceneEx->isRunning() && !sceneEx->wait(6000)) {
            VFP_DEBUG << "流程未能在 6s 内停止，暂不关闭该流程页";
            ui->statusBar->showMessage(QStringLiteral("流程未能在 6 秒内停止，请稍后再关闭"), 5000);
            return;
        }
    }

    // 先从列表中移除场景（这样 removeTab 触发 currentChanged 时索引已同步）
    m_flowScenes.removeAt(index);

    // 再移除标签页（删除 QGraphicsView，使其与场景断开）
    ui->flowTabs->removeTab(index);
    delete tabWidget;

    // 断开执行器与被删场景的关联，并释放场景专属执行器
    if (sceneEx) {
        sceneEx->setFlowScene(nullptr);
        GlobalTriggerManager::instance()->unregisterExecutor(sceneEx);
        // 词条切片 C P3b 期：从表里**摘出**令牌（take 不销毁）⇒ 放掉它即触发收尾。
        std::shared_ptr<FlowExecutorToken> sceneToken = m_flowExecutors.take(scene);
        if (m_flowScenes.isEmpty()) {
            // 最后一个流程：执行器放回占位，等待下次复用
            if (sceneToken)
                m_flowExecutors.insert(nullptr, sceneToken);
            m_executor = sceneEx;
        } else {
            // 释放令牌 ⇒ 令牌析构执行收尾（停止 ＋ 有界等待 ＋ 回收；超时绝不 terminate）
            releaseExecutorToken(sceneToken);
            if (sceneEx == m_executor) {
                // 关闭的是当前激活流程：回收其专属执行器（已摘场景），再切到邻近流程的执行器；
                // 旧实现漏回收 → 每关一次激活页泄漏一个执行器（存量）。
                m_executor = executorForScene(
                    m_flowScenes[qMin(index, m_flowScenes.size() - 1)]);
            }
        }
    }

    // 最后安全删除场景
    delete scene;
}

bool MainWindow::confirmDeleteFlow(FlowScene *scene, int index)
{
    if (!scene)
        return false;
    const QString flowName = scene->flowName().isEmpty()
                                 ? QStringLiteral("流程 %1").arg(index + 1)
                                 : scene->flowName();
    const int moduleCount = int(scene->nodes().size());
    FlowExecutor *ex = executorForScene(scene);
    const bool running = ex && ex->isRunning();

    QMessageBox box(QMessageBox::Question, QStringLiteral("删除流程"),
                    QStringLiteral("确定删除流程「%1」吗？\n\n"
                                   "该流程包含 %2 个模块。\n"
                                   "删除后可通过「编辑 → 恢复最近删除的流程」找回；"
                                   "若方案尚未保存，保存后该流程不会出现在方案文件中。%3")
                        .arg(flowName)
                        .arg(moduleCount)
                        .arg(running ? QStringLiteral("\n\n注意：该流程正在运行，删除会先停止执行。")
                                     : QString()),
                    QMessageBox::NoButton, this);
    QPushButton *deleteBtn = box.addButton(QStringLiteral("删除流程"), QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancelBtn);
    box.exec();
    if (box.clickedButton() != deleteBtn)
        return false;

    // 删除前留快照：X / Ctrl+Shift+T / 右键删除三条路径都从这里过（P0-1）。
    if (ProjectManager *pm = projectManager()) {
        m_lastDeletedFlowJson = pm->sceneToJson(scene);
        m_lastDeletedFlowName = scene->flowName();
        m_lastDeletedFlowMode = scene->flowMode();
        if (m_restoreDeletedFlowAction)
            m_restoreDeletedFlowAction->setEnabled(!m_lastDeletedFlowJson.isEmpty());
    }
    return true;
}

void MainWindow::restoreLastDeletedFlow()
{
    if (m_lastDeletedFlowJson.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("恢复最近删除的流程"),
                                 QStringLiteral("没有可恢复的已删除流程。"));
        return;
    }
    ProjectManager *pm = projectManager();
    if (!pm)
        return;

    createNewFlow();
    const int index = m_flowScenes.size() - 1;
    FlowScene *scene = m_flowScenes.value(index);
    if (!scene)
        return;
    pm->sceneFromJson(m_lastDeletedFlowJson, scene);

    // 尽量恢复原名；若已被占用（例如删除后又新建了同名流程），改用全局唯一名。
    QString name = m_lastDeletedFlowName;
    if (name.isEmpty() || GlobalTriggerManager::instance()->tokenForFlow(name)) {
        name = GlobalTriggerManager::instance()->allocFlowName();
    }
    const QString oldName = scene->flowName();
    GlobalTriggerManager::instance()->unregisterFlow(oldName);
    scene->setFlowName(name);
    if (FlowExecutor *ex = executorForScene(scene))
        ex->setFlowName(name);
    GlobalTriggerManager::instance()->registerFlow(name, scene, m_flowExecutors.value(scene));

    const QString suffix = flowModeSuffix(static_cast<FlowMode>(scene->flowMode()));
    if (ui && ui->flowTabs && index >= 0 && index < ui->flowTabs->count())
        ui->flowTabs->setTabText(index, name + suffix);

    m_lastDeletedFlowJson = QJsonObject();
    if (m_restoreDeletedFlowAction)
        m_restoreDeletedFlowAction->setEnabled(false);
    logMessage(QStringLiteral("已恢复最近删除的流程「%1」").arg(name));
}

void MainWindow::showFlowTabContextMenu(const QPoint &pos)
{
    if (!ui || !ui->flowTabs)
        return;
    QTabBar *bar = ui->flowTabs->tabBar();
    const int index = bar->tabAt(pos);
    if (index < 0 || index >= m_flowScenes.size())
        return;

    // 右键即针对该流程操作（VM 行为），先切页签。
    ui->flowTabs->setCurrentIndex(index);
    FlowScene *scene = m_flowScenes[index];
    if (!scene)
        return;
    FlowExecutor *ex = executorForScene(scene);

    QMenu menu(this);
    QAction *renameAct = menu.addAction(QStringLiteral("重命名流程"));
    QAction *copyAct = menu.addAction(QStringLiteral("拷贝流程"));
    QAction *deleteAct = menu.addAction(QStringLiteral("删除流程"));
    menu.addSeparator();
    QAction *exportAct = menu.addAction(QStringLiteral("导出 Prc…"));
    menu.addSeparator();
    QAction *intervalAct = menu.addAction(
        QStringLiteral("运行间隔：%1 ms").arg(m_flowRunIntervals.value(scene, 0)));
    QAction *ngAct = menu.addAction(QStringLiteral("NG 时停止流程"));
    ngAct->setCheckable(true);
    ngAct->setChecked(ex && ex->stopOnFailure());
    QAction *timeoutAct = menu.addAction(
        QStringLiteral("流程超时：%1 s").arg(m_flowTimeouts.value(scene, 0) / 1000));

    connect(renameAct, &QAction::triggered, this, [this, index]() { renameFlow(index); });
    connect(copyAct, &QAction::triggered, this, [this, index]() { duplicateFlow(index); });
    connect(deleteAct, &QAction::triggered, this, [this, index]() { onCloseFlowTab(index); });
    connect(exportAct, &QAction::triggered, this, [this, index]() { exportFlowToFile(index); });
    connect(intervalAct, &QAction::triggered, this, [this, scene]() {
        bool ok = false;
        const int ms = QInputDialog::getInt(this, QStringLiteral("运行间隔"),
                                            QStringLiteral("每轮之间的间隔（毫秒，0 = 连续运行）："),
                                            m_flowRunIntervals.value(scene, 0), 0, 60000, 100, &ok);
        if (!ok)
            return;
        m_flowRunIntervals[scene] = ms;
        if (FlowExecutor *e = executorForScene(scene))
            e->setLoopIntervalMs(ms);
    });
    connect(ngAct, &QAction::toggled, this, [this, scene](bool on) {
        if (FlowExecutor *e = executorForScene(scene))
            e->setStopOnFailure(on);
    });
    connect(timeoutAct, &QAction::triggered, this, [this, scene]() {
        bool ok = false;
        const int secs = QInputDialog::getInt(this, QStringLiteral("流程超时"),
                                              QStringLiteral("单轮超时（秒，0 = 不限）："),
                                              m_flowTimeouts.value(scene, 0) / 1000, 0, 3600, 1, &ok);
        if (!ok)
            return;
        m_flowTimeouts[scene] = secs * 1000;
    });
    menu.exec(bar->mapToGlobal(pos));
}

void MainWindow::renameFlow(int index)
{
    if (index < 0 || index >= m_flowScenes.size())
        return;
    FlowScene *scene = m_flowScenes[index];
    if (!scene)
        return;
    const QString oldName = scene->flowName();
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名流程"),
                                               QStringLiteral("流程名称："), QLineEdit::Normal,
                                               oldName, &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == oldName)
        return;
    if (GlobalTriggerManager::instance()->tokenForFlow(name)) {
        QMessageBox::warning(this, QStringLiteral("重命名流程"),
                             QStringLiteral("流程名「%1」已被占用，请换一个名称。").arg(name));
        return;
    }

    // 触发路由按名绑定：先注销旧名，改名后重新注册（避免触发指向旧名/同名覆盖）。
    GlobalTriggerManager::instance()->unregisterFlow(oldName);
    scene->setFlowName(name);
    if (FlowExecutor *ex = executorForScene(scene))
        ex->setFlowName(name);
    GlobalTriggerManager::instance()->registerFlow(name, scene, m_flowExecutors.value(scene));

    const QString suffix = flowModeSuffix(static_cast<FlowMode>(scene->flowMode()));
    if (ui && ui->flowTabs && index < ui->flowTabs->count())
        ui->flowTabs->setTabText(index, name + suffix);
    logMessage(QStringLiteral("流程已重命名：%1 → %2").arg(oldName, name));
}

void MainWindow::duplicateFlow(int index)
{
    if (index < 0 || index >= m_flowScenes.size())
        return;
    FlowScene *src = m_flowScenes[index];
    ProjectManager *pm = projectManager();
    if (!src || !pm)
        return;
    const QJsonObject json = pm->sceneToJson(src);

    createNewFlow();
    const int newIndex = m_flowScenes.size() - 1;
    FlowScene *dst = m_flowScenes.value(newIndex);
    if (!dst)
        return;
    pm->sceneFromJson(json, dst);
    m_flowModes[dst] = static_cast<FlowMode>(dst->flowMode());

    // 副本命名：原名 + _副本，重名时追加序号。
    const QString base = src->flowName().isEmpty() ? QStringLiteral("流程") : src->flowName();
    QString name = base + QStringLiteral("_副本");
    int n = 2;
    while (GlobalTriggerManager::instance()->tokenForFlow(name))
        name = base + QStringLiteral("_副本%1").arg(n++);
    const QString oldName = dst->flowName();
    GlobalTriggerManager::instance()->unregisterFlow(oldName);
    dst->setFlowName(name);
    if (FlowExecutor *ex = executorForScene(dst))
        ex->setFlowName(name);
    GlobalTriggerManager::instance()->registerFlow(name, dst, m_flowExecutors.value(dst));

    const QString suffix = flowModeSuffix(static_cast<FlowMode>(dst->flowMode()));
    if (ui && ui->flowTabs && newIndex < ui->flowTabs->count())
        ui->flowTabs->setTabText(newIndex, name + suffix);
    logMessage(QStringLiteral("已拷贝流程「%1」").arg(name));
}

void MainWindow::exportFlowToFile(int index)
{
    if (index < 0 || index >= m_flowScenes.size())
        return;
    FlowScene *scene = m_flowScenes[index];
    ProjectManager *pm = projectManager();
    if (!scene || !pm)
        return;
    const QString suggest = scene->flowName().isEmpty() ? QStringLiteral("流程") : scene->flowName();
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出流程"),
                                                      suggest + QStringLiteral(".prc"),
                                                      QStringLiteral("流程文件 (*.prc);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出流程"),
                             QStringLiteral("无法写入文件：%1").arg(file.errorString()));
        return;
    }
    file.write(QJsonDocument(pm->sceneToJson(scene)).toJson(QJsonDocument::Indented));
    file.close();
    logMessage(QStringLiteral("流程已导出：%1").arg(path));
}

void MainWindow::syncCanvasLockAction()
{
    FlowScene *scene = currentFlowScene();
    if (m_lockCanvasAction) {
        QSignalBlocker blocker(m_lockCanvasAction);
        m_lockCanvasAction->setChecked(scene && scene->isUserLocked());
    }
    if (m_undoAction)
        m_undoAction->setEnabled(scene && scene->canUndo());
    if (m_redoAction)
        m_redoAction->setEnabled(scene && scene->canRedo());
}

void MainWindow::updateUserButton()
{
    SessionManager *sm = SessionManager::instance();
    if (!m_userButton)
        return;
    if (sm && sm->isLoggedIn()) {
        m_userButton->setText(QStringLiteral("用户：%1 [%2]").arg(sm->currentUser(), sm->currentRole()));
        m_userButton->setToolTip(QStringLiteral("当前登录用户；可切换角色或注销"));
    } else {
        m_userButton->setText(QStringLiteral("未登录"));
        m_userButton->setToolTip(QStringLiteral("当前无登录会话"));
    }
}

void MainWindow::switchUser()
{
    UserLoginDialog loginDialog(this);
    if (loginDialog.exec() != QDialog::Accepted)
        return;
    const QString user = loginDialog.loggedInUser();
    const QString role = loginDialog.loggedInRole();
    SessionManager::instance()->login(user, role);
    setWindowTitle(QStringLiteral("视觉方案工作台 - %1 [%2]").arg(user, role));
    updateUserButton();
    applyPermissionRestrictions();
    logMessage(QStringLiteral("已切换用户：%1 [%2]").arg(user, role));
}

void MainWindow::logoutUser()
{
    const auto answer = QMessageBox::question(this, QStringLiteral("注销"),
                                               QStringLiteral("注销当前用户并返回登录界面？"),
                                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    // 停掉所有流程，避免注销后后台流程仍以旧身份运行
    for (FlowScene *scene : m_flowScenes) {
        if (FlowExecutor *ex = executorForScene(scene)) {
            ex->stopExecution();
            ex->wait(1000);
        }
    }
    SessionManager::instance()->logout();
    updateUserButton();

    UserLoginDialog loginDialog(this);
    if (loginDialog.exec() != QDialog::Accepted) {
        // A1-②：注销后取消登录 = 结束会话，直接退出（不允许无会话继续操作）。
        QApplication::quit();
        return;
    }
    SessionManager::instance()->login(loginDialog.loggedInUser(), loginDialog.loggedInRole());
    const QString user = loginDialog.loggedInUser();
    const QString role = loginDialog.loggedInRole();
    setWindowTitle(QStringLiteral("视觉方案工作台 - %1 [%2]").arg(user, role));
    updateUserButton();
    applyPermissionRestrictions();
    logMessage(QStringLiteral("用户已登录：%1 [%2]").arg(user, role));
}

void MainWindow::onFlowTimeout()
{
    // 超时属于「发起运行的那个流程」：切页签不影响判定（否则会停错流程）。
    FlowScene *scene = m_timeoutScene;
    if (!scene || !m_flowScenes.contains(scene)) {   // 场景已关闭：指针只用于比较，不解引用
        m_timeoutScene = nullptr;
        return;
    }
    FlowExecutor *ex = executorForScene(scene);
    if (!ex || !ex->isRunning())
        return;
    const int timeoutMs = m_flowTimeouts.value(scene, 0);
    if (timeoutMs <= 0)
        return;
    logMessage(QStringLiteral("流程「%1」单轮超时（%2 ms），已请求停止")
                   .arg(scene->flowName())
                   .arg(timeoutMs));
    ex->stopExecution();
    m_timeoutScene = nullptr;
}

void MainWindow::onSwitchLanguage()
{
    // 切换语言
    logMessage("切换语言");
}

void MainWindow::logMessage(const QString &message)
{
    // 运行界面「界面日志」控件订阅同一日志流（手册：实时显示软件的日志信息）
    UiLogBus::instance()->publish(message);
    ui->logTextEdit->appendPlainText(QString("[%1] %2").arg(QDateTime::currentDateTime().toString()).arg(message));
    // 折叠态下始终把视口滚到最新一行（否则"最近一条"看不到）
    if (!m_logExpanded && ui->logTextEdit->verticalScrollBar())
        ui->logTextEdit->verticalScrollBar()->setValue(
            ui->logTextEdit->verticalScrollBar()->maximum());
}

void MainWindow::updateLogExpandButton()
{
    if (!m_logExpandBtn)
        return;
    m_logExpandBtn->setText(m_logExpanded ? tr("收起") : tr("展开"));
    m_logExpandBtn->setToolTip(m_logExpanded
                                   ? tr("收起为最近一条日志")
                                   : tr("展开查看完整日志"));
}

void MainWindow::setLogExpanded(bool on)
{
    if (!ui || !ui->logTextEdit)
        return;
    m_logExpanded = on;
    if (on) {
        ui->logTextEdit->setMinimumHeight(0);
        ui->logTextEdit->setMaximumHeight(QWIDGETSIZE_MAX);
        ui->logTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    } else {
        const int lineHeight = ui->logTextEdit->fontMetrics().height();
        const int compactHeight = lineHeight + 12;   // 单行 + 边框/内边距
        ui->logTextEdit->setFixedHeight(compactHeight);
        ui->logTextEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        if (ui->logTextEdit->verticalScrollBar())
            ui->logTextEdit->verticalScrollBar()->setValue(
                ui->logTextEdit->verticalScrollBar()->maximum());
    }
    updateLogExpandButton();
}

void MainWindow::updateSchemeLabel(const QString &filePath)
{
    if (!m_schemeLabel)
        return;
    const QString name = filePath.isEmpty()
                             ? tr("未命名方案")
                             : QFileInfo(filePath).completeBaseName();
    m_schemeLabel->setText(tr("方案：%1").arg(name));
    m_schemeLabel->setToolTip(filePath.isEmpty() ? tr("尚未保存到文件") : filePath);
}

namespace {

/// 采样当前资源占用：CPU / 内存 / 指定目录所在卷的已用比例（-1 = 未获取）。
/// 状态栏右上角与「异常信息收集」共用，避免两处采样口径分叉。
void sampleResourceUsage(const QString &volumePath, int *cpuPercent, int *memoryPercent,
                         int *diskPercent)
{
    *cpuPercent = -1;
    *memoryPercent = -1;
    *diskPercent = -1;

#ifdef Q_OS_WIN
    // CPU：两次采样 GetSystemTimes 的差值（kernel 时间含 idle，故空闲 = idleDelta）
    static ULONGLONG prevIdle = 0, prevKernel = 0, prevUser = 0;
    ULONGLONG idle = 0, kernel = 0, user = 0;
    if (GetSystemTimes(reinterpret_cast<FILETIME *>(&idle),
                       reinterpret_cast<FILETIME *>(&kernel),
                       reinterpret_cast<FILETIME *>(&user))) {
        const ULONGLONG total = (kernel + user) - (prevKernel + prevUser);
        const ULONGLONG idleDelta = idle - prevIdle;
        if (prevKernel != 0 && total > 0)
            *cpuPercent = static_cast<int>((total - idleDelta) * 100 / total);
        prevIdle = idle;
        prevKernel = kernel;
        prevUser = user;
    }
    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus))
        *memoryPercent = static_cast<int>(memStatus.dwMemoryLoad);
#endif

    // 磁盘：指定目录所在卷的已用比例（目录不存在时 QStorageInfo 自身返回无效）
    QStorageInfo storage(volumePath);
    storage.refresh();
    if (storage.isValid() && storage.bytesTotal() > 0) {
        const qint64 used = storage.bytesTotal() - storage.bytesAvailable();
        *diskPercent = static_cast<int>(used * 100 / storage.bytesTotal());
    }
}

} // namespace

void MainWindow::refreshResourceUsage()
{
    if (!m_resourceLabel)
        return;

    // 磁盘统计口径：方案目录（未保存时用当前工作目录）所在卷
    const QString schemePath = m_projectManager ? m_projectManager->lastFilePath() : QString();
    const QString volumePath = schemePath.isEmpty()
                                   ? QDir::currentPath()
                                   : QFileInfo(schemePath).absolutePath();
    int cpuPercent = -1;
    int memoryPercent = -1;
    int diskPercent = -1;
    sampleResourceUsage(volumePath, &cpuPercent, &memoryPercent, &diskPercent);

    // 运行策略·诊断模式（VM 4.4 手册 11.4）：任一资源占用过高时告警；普通模式静默
    if (m_resourceDiagnosticMode) {
        QStringList high;
        if (cpuPercent >= 90)
            high << QStringLiteral("CPU %1%").arg(cpuPercent);
        if (memoryPercent >= 90)
            high << QStringLiteral("内存 %1%").arg(memoryPercent);
        if (diskPercent >= 90)
            high << QStringLiteral("硬盘 %1%").arg(diskPercent);
        // 节流：同一类告警每 30 秒最多刷一次（2 秒一采，否则日志会被告警淹没）
        if (!high.isEmpty()
            && (!m_diagWarnTimer.isValid() || m_diagWarnTimer.elapsed() >= 30000)) {
            m_diagWarnTimer.start();
            const QString detail = high.join(QStringLiteral(" / "));
            logMessage(tr("资源诊断：%1 占用过高，建议检查流程负载或降低并发").arg(detail));
            statusBar()->showMessage(tr("资源诊断：%1 占用过高").arg(detail), 8000);
        }
    }

    auto fmt = [](int percent, const QString &label) {
        return percent < 0 ? QStringLiteral("%1 --").arg(label)
                           : QStringLiteral("%1 %2%").arg(label).arg(percent);
    };
    m_resourceLabel->setText(QStringLiteral("%1  |  %2  |  %3")
                                 .arg(fmt(cpuPercent, QStringLiteral("CPU")),
                                      fmt(memoryPercent, QStringLiteral("内存")),
                                      fmt(diskPercent, QStringLiteral("硬盘"))));
}

void MainWindow::maybeShowWelcomeOnStartup()
{
    if (QSettings().value(QStringLiteral("welcome/dontShow"), false).toBool())
        return;
    showWelcomePage();
}

void MainWindow::showWelcomePage()
{
    // VM 4.4 口径：欢迎页是独立起步界面，出现时主编辑界面不应同时可见
    //（否则就是截图里“欢迎窗浮在流程画布上”的共存状态）。关闭欢迎页后再回到主界面。
    const bool hadMainVisible = isVisible();
    if (hadMainVisible)
        hide();

    // 示例方案随安装目录分发；没有示例包时欢迎页不展示「打开示例方案」，
    // 避免一个点了只会报错的死按钮。文件菜单的「打开示例方案…」共用同一份探测。
    const QString exampleFile = bundledExamplePath();

    WelcomeDialog dialog(m_recentFiles ? m_recentFiles->files() : QStringList(), this,
                         !exampleFile.isEmpty());
    const bool accepted = dialog.exec() == QDialog::Accepted;

    if (hadMainVisible) {
        show();
        raise();
        activateWindow();
    }
    if (!accepted)
        return;

    if (dialog.dontShowAgain()) {
        QSettings settings;
        settings.setValue(QStringLiteral("welcome/dontShow"), true);
    }

    switch (dialog.chosenAction()) {
    case WelcomeDialog::Action::NewProject:
        onNewProject();
        break;
    case WelcomeDialog::Action::OpenProject:
        onLoadProject();
        break;
    case WelcomeDialog::Action::OpenRecent:
        if (!dialog.selectedProjectPath().isEmpty())
            loadProjectFile(dialog.selectedProjectPath());
        break;
    case WelcomeDialog::Action::OpenExample:
        if (!exampleFile.isEmpty())
            loadProjectFile(exampleFile);
        break;
    case WelcomeDialog::Action::OpenManual:
        openManualDialog();
        break;
    case WelcomeDialog::Action::None:
        break;
    }
}

QString MainWindow::bundledExamplePath() const
{
    // 示例方案随安装目录分发：可执行目录及上级的 examples/ 都找一遍。
    const QString appDir = QApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("examples")),
        QDir(appDir).filePath(QStringLiteral("../examples")),
        QDir(appDir).filePath(QStringLiteral("../../examples")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("examples")),
    };
    for (const QString &dirPath : candidates) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.vfp"),
                                                QDir::Files, QDir::Name);
        if (!files.isEmpty())
            return dir.absoluteFilePath(files.first());
    }
    return {};
}

void MainWindow::onOpenExampleProject()
{
    // P1-6 文件菜单：直接载入示例方案（不隐藏主界面、不经过欢迎页）
    const QString path = bundledExamplePath();
    if (path.isEmpty()) {
        QMessageBox::information(this, tr("打开示例方案"),
                                 tr("未找到内置示例方案：请确认安装目录下存在 examples/*.vfp。"));
        return;
    }
    loadProjectFile(path);
}

void MainWindow::openComprehensiveConfig()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("综合配置"));
    dialog.setMinimumSize(620, 420);

    auto *root = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    root->addWidget(tabs);

    auto makePage = [&](const QString &title) {
        auto *page = new QWidget(tabs);
        auto *grid = new QGridLayout(page);
        grid->setContentsMargins(14, 14, 14, 14);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(10);
        tabs->addTab(page, title);
        return grid;
    };
    auto addEntry = [this, &dialog](QGridLayout *grid, int row, int col,
                                    const QString &text, QAction *action,
                                    const QString &tip) {
        QPushButton *button = new QPushButton(text);
        button->setMinimumHeight(38);
        button->setToolTip(tip);
        grid->addWidget(button, row, col);
        if (!action) {
            button->setEnabled(false);
            return;
        }
        QPointer<QAction> safeAction(action);
        connect(button, &QPushButton::clicked, &dialog,
                [&dialog, safeAction, button]() {
                    dialog.done(QDialog::Accepted);
                    if (safeAction)
                        QTimer::singleShot(0, safeAction.data(), &QAction::trigger);
                    Q_UNUSED(button)
                });
    };

    auto *acqGrid = makePage(tr("采集"));
    addEntry(acqGrid, 0, 0, tr("相机管理"), ui->actionCameraConfig,
             tr("添加/删除相机、设置像素格式与触发源"));
    addEntry(acqGrid, 0, 1, tr("全局触发"), ui->actionGlobalTrigger,
             tr("配置软触发/硬触发与触发源"));
    {
        QPushButton *refreshBtn = new QPushButton(tr("刷新全部 MVS 像素格式"));
        refreshBtn->setMinimumHeight(38);
        refreshBtn->setToolTip(tr("重新向所有 MVS 相机查询可用像素格式"));
        acqGrid->addWidget(refreshBtn, 1, 0);
        connect(refreshBtn, &QPushButton::clicked, this, [this]() { refreshAllMvsPixelFormats(); });
    }
    addEntry(acqGrid, 1, 1, tr("图像层自检"), nullptr,
             tr("帮助菜单可执行 HALCON 图像层自检"));

    auto *commGrid = makePage(tr("通讯"));
    addEntry(commGrid, 0, 0, tr("通讯设备"), ui->actionCommDevice, tr("增删通讯设备与连接参数"));
    addEntry(commGrid, 0, 1, tr("接收事件"), ui->actionCommReceiveEvent, tr("配置通讯接收事件绑定"));
    addEntry(commGrid, 1, 0, tr("发送事件"), ui->actionCommSendEvent, tr("配置通讯发送事件绑定"));
    addEntry(commGrid, 1, 1, tr("心跳"), ui->actionCommHeartbeat, tr("配置通讯心跳与超时"));
    addEntry(commGrid, 2, 0, tr("通讯监视"), ui->actionCommMonitor, tr("查看通讯收发报文"));

    auto *runGrid = makePage(tr("运行与数据"));
    addEntry(runGrid, 0, 0, tr("全局变量"), ui->actionGlobalVariables, tr("管理 {global.变量}"));
    addEntry(runGrid, 0, 1, tr("参数查找工具"), ui->actionParameterSearch, tr("按流程/模块查找参数并生成调用代码"));
    addEntry(runGrid, 1, 0, tr("用户管理"), ui->actionUserManagement, tr("用户、角色与权限"));
    addEntry(runGrid, 1, 1, tr("配方管理"), ui->actionRecipeManager, tr("切换/保存配方"));
    addEntry(runGrid, 2, 0, tr("检测结果"), ui->actionInspectionResults, tr("查看历史检测结果"));
    addEntry(runGrid, 2, 1, tr("报表"), ui->actionReport, tr("导出统计报表"));

    auto *uiGrid = makePage(tr("界面"));
    {
        auto *welcomeCheck = new QCheckBox(tr("启动时显示欢迎页"));
        welcomeCheck->setChecked(!QSettings().value(QStringLiteral("welcome/dontShow"), false).toBool());
        uiGrid->addWidget(welcomeCheck, 0, 0, 1, 2);
        connect(welcomeCheck, &QCheckBox::toggled, this, [](bool on) {
            QSettings settings;
            settings.setValue(QStringLiteral("welcome/dontShow"), !on);
        });
        auto *logCheck = new QCheckBox(tr("日志栏默认展开显示全量"));
        logCheck->setChecked(QSettings().value(QStringLiteral("ui/logExpanded"), false).toBool());
        uiGrid->addWidget(logCheck, 1, 0, 1, 2);
        connect(logCheck, &QCheckBox::toggled, this, [this](bool on) {
            setLogExpanded(on);
            QSettings settings;
            settings.setValue(QStringLiteral("ui/logExpanded"), on);
        });
        auto *resetLayoutBtn = new QPushButton(tr("恢复默认停靠布局"));
        resetLayoutBtn->setMinimumHeight(38);
        uiGrid->addWidget(resetLayoutBtn, 2, 0, 1, 2);
        connect(resetLayoutBtn, &QPushButton::clicked, this, [this]() { resetDockLayout(); });
    }

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttonBox->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttonBox);

    dialog.exec();
}

void MainWindow::resetDockLayout()
{
    addDockWidget(Qt::LeftDockWidgetArea, ui->toolDock);
    // 流程编辑是中央控件（setupDockWidgets 里已从 dock 迁出），不要再把旧 flowDock 加回来
    addDockWidget(Qt::RightDockWidgetArea, ui->imageDock);
    addDockWidget(Qt::RightDockWidgetArea, ui->paramDock);
    if (ui->imageDock && ui->paramDock) {
        // 默认形态是右侧两个可切换子页面（FR4.3），不是上下/左右并排
        tabifyDockWidget(ui->imageDock, ui->paramDock);
        ui->imageDock->raise();
    }
    addDockWidget(Qt::BottomDockWidgetArea, ui->logDock);
    // 结果区固定在图像区下方（与启动时的口径一致）
    if (QDockWidget *resultDock = m_auxPanels ? m_auxPanels->resultTableDock() : nullptr) {
        removeDockWidget(resultDock);
        addDockWidget(Qt::RightDockWidgetArea, resultDock);
        resultDock->show();
    }
    m_dockLayoutRestored = false;   // 显式恢复默认布局：下次关闭时按新布局存档
    applyDefaultDockSizes();
}

void MainWindow::openSoftwareSettings()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("softwareSettingsDialog"));
    dialog.setWindowTitle(tr("软件设置"));
    dialog.setMinimumWidth(480);
    auto *root = new QVBoxLayout(&dialog);

    auto *intro = new QLabel(
        tr("软件级选项：即时生效并保存在本机配置（不写入当前方案文件）。"), &dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    // 启动
    auto *welcomeCheck = new QCheckBox(tr("启动时显示欢迎页"), &dialog);
    welcomeCheck->setChecked(
        !QSettings().value(QStringLiteral("welcome/dontShow"), false).toBool());
    connect(welcomeCheck, &QCheckBox::toggled, &dialog, [](bool on) {
        QSettings().setValue(QStringLiteral("welcome/dontShow"), !on);
    });
    form->addRow(tr("启动"), welcomeCheck);

    // 日志
    auto *logCheck = new QCheckBox(tr("日志栏默认展开显示全量"), &dialog);
    logCheck->setChecked(m_logExpanded);
    connect(logCheck, &QCheckBox::toggled, this, [this](bool on) {
        setLogExpanded(on);
        QSettings().setValue(QStringLiteral("ui/logExpanded"), on);
    });
    form->addRow(tr("日志"), logCheck);

    // 自动保存（0 = 关闭；即时生效）
    auto *autoSaveSpin = new QSpinBox(&dialog);
    autoSaveSpin->setRange(0, 3600);
    autoSaveSpin->setSuffix(tr(" 秒"));
    autoSaveSpin->setSpecialValueText(tr("关闭"));
    autoSaveSpin->setValue(
        QSettings().value(QStringLiteral("recovery/autoSaveIntervalSec"), 60).toInt());
    autoSaveSpin->setToolTip(tr("0 = 关闭自动保存；关闭后仍保留关窗时的未保存确认"));
    connect(autoSaveSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int seconds) {
        QSettings().setValue(QStringLiteral("recovery/autoSaveIntervalSec"), seconds);
        if (seconds <= 0) {
            if (m_autoSaveTimer)
                m_autoSaveTimer->stop();
            return;
        }
        if (!m_autoSaveTimer) {
            m_autoSaveTimer = new QTimer(this);
            connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSaveTick);
        }
        m_autoSaveTimer->start(seconds * 1000);
    });
    form->addRow(tr("自动保存"), autoSaveSpin);

    // 崩溃恢复提示
    auto *promptStartCheck = new QCheckBox(tr("启动时提示可恢复的未保存改动"), &dialog);
    promptStartCheck->setChecked(
        QSettings().value(QStringLiteral("recovery/promptOnStartup"), true).toBool());
    connect(promptStartCheck, &QCheckBox::toggled, &dialog, [](bool on) {
        QSettings().setValue(QStringLiteral("recovery/promptOnStartup"), on);
    });
    auto *promptCloseCheck = new QCheckBox(tr("关闭时提示未保存改动"), &dialog);
    promptCloseCheck->setChecked(
        QSettings().value(QStringLiteral("recovery/promptOnClose"), true).toBool());
    connect(promptCloseCheck, &QCheckBox::toggled, &dialog, [](bool on) {
        QSettings().setValue(QStringLiteral("recovery/promptOnClose"), on);
    });
    form->addRow(tr("崩溃恢复"), promptStartCheck);
    form->addRow(promptCloseCheck);

    // 资源诊断（超限告警）
    auto *diagCheck = new QCheckBox(tr("资源诊断模式（CPU/内存/硬盘超限告警）"), &dialog);
    diagCheck->setChecked(m_resourceDiagnosticMode);
    connect(diagCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_resourceDiagnosticMode = on;
        QSettings().setValue(QStringLiteral("flow/resourceDiagnosticMode"), on);
        refreshResourceUsage();
    });
    form->addRow(tr("诊断"), diagCheck);

    root->addLayout(form);
    root->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    QPushButton *resetBtn =
        buttons->addButton(tr("恢复默认停靠布局"), QDialogButtonBox::ResetRole);
    connect(resetBtn, &QPushButton::clicked, this, [this]() { resetDockLayout(); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    dialog.exec();
}

void MainWindow::openSchemeSettings()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("schemeSettingsDialog"));
    dialog.setWindowTitle(tr("方案设置"));
    dialog.setMinimumSize(540, 420);
    auto *root = new QVBoxLayout(&dialog);

    auto *infoTitle = new QLabel(tr("当前方案"), &dialog);
    infoTitle->setStyleSheet(QStringLiteral("font-weight:bold;"));
    root->addWidget(infoTitle);

    auto *form = new QFormLayout();
    const QString path = m_projectManager ? m_projectManager->lastFilePath() : QString();
    auto *pathLabel = new QLabel(
        path.isEmpty() ? VmSharedText::untitledProjectName() : QDir::toNativeSeparators(path),
        &dialog);
    pathLabel->setWordWrap(true);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("文件"), pathLabel);

    int nodeCount = 0;
    for (FlowScene *scene : qAsConst(m_flowScenes)) {
        if (scene)
            nodeCount += scene->nodes().size();
    }
    form->addRow(tr("流程"), new QLabel(tr("%1 个").arg(m_flowScenes.size()), &dialog));
    form->addRow(tr("模块"), new QLabel(tr("%1 个").arg(nodeCount), &dialog));

    const QFileInfo fileInfo(path);
    form->addRow(tr("文件大小"),
                 new QLabel(fileInfo.exists()
                                ? tr("%1 KB").arg(QString::number(fileInfo.size() / 1024.0, 'f', 1))
                                : tr("—"),
                            &dialog));
    form->addRow(tr("最后保存"),
                 new QLabel(fileInfo.exists()
                                ? fileInfo.lastModified().toString(
                                      QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                : tr("—"),
                            &dialog));
    root->addLayout(form);

    root->addWidget(new QLabel(tr("流程与运行模式（切换请回流程栏）"), &dialog));
    auto *table = new QTableWidget(static_cast<int>(m_flowScenes.size()), 2, &dialog);
    table->setHorizontalHeaderLabels({tr("流程"), tr("运行模式")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    for (int i = 0; i < m_flowScenes.size(); ++i) {
        FlowScene *scene = m_flowScenes.at(i);
        const FlowMode mode = m_flowModes.value(scene, FlowMode::SoftwareTrigger);
        const QString modeText = (mode == FlowMode::Continuous)
                                     ? tr("连续模式")
                                     : (mode == FlowMode::HardwareTrigger) ? tr("硬触发模式")
                                                                           : tr("软触发模式");
        table->setItem(
            i, 0, new QTableWidgetItem(scene ? scene->flowName() : QStringLiteral("—")));
        table->setItem(i, 1, new QTableWidgetItem(modeText));
    }
    root->addWidget(table, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    auto addSchemeAction = [&](const QString &text, QAction *action) {
        QPushButton *button = buttons->addButton(text, QDialogButtonBox::ActionRole);
        if (!action) {
            button->setEnabled(false);
            return;
        }
        button->setEnabled(action->isEnabled());
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
    };
    addSchemeAction(tr("保存方案"), ui->actionSaveScheme);
    addSchemeAction(tr("方案另存为"), m_actionSaveAs);
    addSchemeAction(tr("导出加密/只读"), m_actionExportEncrypted);
    addSchemeAction(tr("打包模型与依赖"), m_packModelsAct);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);
    dialog.exec();
}

void MainWindow::openCalibrationWizardDialog()
{
    CalibrationWizardDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const CalibrationWizardSpec spec = CalibrationWizard::specFor(dlg.selectedType());
    logMessage(tr("标定配置引导：%1（%2 个引导步骤）")
                   .arg(spec.name)
                   .arg(spec.steps.size()));
}

void MainWindow::openCalibrationBoardDialog()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("calibrationBoardDialog"));
    dialog.setWindowTitle(tr("标定板生成"));
    dialog.setMinimumWidth(560);
    auto *root = new QVBoxLayout(&dialog);

    auto *intro = new QLabel(
        tr("按物理尺寸与打印 DPI 生成标定板图片。棋盘格的内角点数 = 方格数 - 1；"
           "打印时请选 100% 比例，勿缩放。"),
        &dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *typeCombo = new QComboBox(&dialog);
    typeCombo->addItem(tr("棋盘格"), static_cast<int>(CalibrationBoardType::Chessboard));
    typeCombo->addItem(tr("圆点阵列"), static_cast<int>(CalibrationBoardType::CirclesGrid));
    form->addRow(tr("图案类型"), typeCombo);

    auto *xSpin = new QSpinBox(&dialog);
    xSpin->setRange(3, 100);
    xSpin->setValue(11);
    form->addRow(tr("横向方格/圆点"), xSpin);

    auto *ySpin = new QSpinBox(&dialog);
    ySpin->setRange(3, 100);
    ySpin->setValue(8);
    form->addRow(tr("纵向方格/圆点"), ySpin);

    auto *sizeSpin = new QDoubleSpinBox(&dialog);
    sizeSpin->setRange(0.1, 1000.0);
    sizeSpin->setDecimals(1);
    sizeSpin->setValue(10.0);
    sizeSpin->setSuffix(tr(" mm"));
    sizeSpin->setToolTip(tr("棋盘格为方格边长；圆点板为相邻圆心间距"));
    form->addRow(tr("格距"), sizeSpin);

    auto *marginSpin = new QDoubleSpinBox(&dialog);
    marginSpin->setRange(0.0, 1000.0);
    marginSpin->setDecimals(1);
    marginSpin->setValue(10.0);
    marginSpin->setSuffix(tr(" mm"));
    form->addRow(tr("四周留白"), marginSpin);

    auto *dpiSpin = new QSpinBox(&dialog);
    dpiSpin->setRange(72, 1200);
    dpiSpin->setValue(300);
    dpiSpin->setToolTip(tr("打印分辨率：激光打印机常用 300 / 600 DPI"));
    form->addRow(tr("打印 DPI"), dpiSpin);

    root->addLayout(form);

    // 预览：固定画布尺寸，按整板比例缩放显示
    auto *previewLabel = new QLabel(&dialog);
    previewLabel->setObjectName(QStringLiteral("calibrationBoardPreview"));
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setFixedSize(520, 280);
    previewLabel->setFrameShape(QFrame::StyledPanel);
    previewLabel->setFrameShadow(QFrame::Sunken);
    previewLabel->setStyleSheet(QStringLiteral("background:#f0f0f0;"));
    root->addWidget(previewLabel, 0, Qt::AlignHCenter);

    auto *infoLabel = new QLabel(&dialog);
    infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(infoLabel);

    auto *statusLabel = new QLabel(&dialog);
    statusLabel->setStyleSheet(QStringLiteral("color:#2e7d32;"));
    statusLabel->setWordWrap(true);
    root->addWidget(statusLabel);

    auto *errorLabel = new QLabel(&dialog);
    errorLabel->setStyleSheet(QStringLiteral("color:#c62828;"));   // 红：参数错误直接可见
    errorLabel->setWordWrap(true);
    root->addWidget(errorLabel);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, &dialog);
    QPushButton *saveButton = buttons->button(QDialogButtonBox::Save);
    saveButton->setText(tr("生成并保存…"));
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    // 控件 → 参数规格（预览与保存共用的唯一装配点，避免两处取值口径分叉）
    const auto buildSpec = [typeCombo, xSpin, ySpin, sizeSpin, marginSpin, dpiSpin]() {
        CalibrationBoardSpec spec;
        spec.type = static_cast<CalibrationBoardType>(typeCombo->currentData().toInt());
        spec.squaresX = xSpin->value();
        spec.squaresY = ySpin->value();
        spec.squareSizeMm = sizeSpin->value();
        spec.marginMm = marginSpin->value();
        spec.dpi = dpiSpin->value();
        return spec;
    };

    // 预览刷新：物理尺寸按整板 mm 标注；预览按等效 DPI 限制边长（几何比例与成品一致），
    // 避免每次改参数都渲染整张大图卡住界面。
    const auto refresh = [this, buildSpec, previewLabel, infoLabel, statusLabel, errorLabel,
                          saveButton]() {
        const CalibrationBoardSpec spec = buildSpec();
        QString error;
        const QSize fullSize = CalibrationBoard::pixelSize(spec, &error);
        if (fullSize.isEmpty()) {
            previewLabel->clear();
            previewLabel->setText(tr("参数无效"));
            infoLabel->clear();
            statusLabel->clear();
            errorLabel->setText(error);
            saveButton->setEnabled(false);
            return;
        }

        const double widthMm = spec.squaresX * spec.squareSizeMm + 2.0 * spec.marginMm;
        const double heightMm = spec.squaresY * spec.squareSizeMm + 2.0 * spec.marginMm;
        infoLabel->setText(tr("整板 %1 × %2 mm（%3 × %4 px）")
                               .arg(QString::number(widthMm, 'f', 1),
                                    QString::number(heightMm, 'f', 1))
                               .arg(fullSize.width())
                               .arg(fullSize.height()));
        errorLabel->clear();
        statusLabel->clear();
        saveButton->setEnabled(true);

        CalibrationBoardSpec previewSpec = spec;
        const int longestEdge = qMax(fullSize.width(), fullSize.height());
        const int previewLimit = 720;
        if (longestEdge > previewLimit) {
            // 等效 DPI 保底 72：极端大板预览略超 720 px，也比参数校验失败好
            previewSpec.dpi = qMax(72, int(spec.dpi * double(previewLimit) / longestEdge));
        }
        const QImage preview = CalibrationBoard::render(previewSpec);
        if (preview.isNull()) {
            previewLabel->clear();
            previewLabel->setText(tr("预览生成失败"));
            return;
        }
        previewLabel->setPixmap(QPixmap::fromImage(preview).scaled(
            500, 260, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };

    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [refresh](int) { refresh(); });
    connect(xSpin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [refresh](int) { refresh(); });
    connect(ySpin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [refresh](int) { refresh(); });
    connect(sizeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &dialog,
            [refresh](double) { refresh(); });
    connect(marginSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &dialog,
            [refresh](double) { refresh(); });
    connect(dpiSpin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [refresh](int) { refresh(); });

    connect(saveButton, &QPushButton::clicked, &dialog,
            [this, &dialog, buildSpec, statusLabel, errorLabel]() {
        const CalibrationBoardSpec spec = buildSpec();
        QString error;
        const QSize size = CalibrationBoard::pixelSize(spec, &error);
        if (size.isEmpty()) {
            errorLabel->setText(error);
            return;
        }

        // 默认文件名带上关键参数，多次生成并存时可一眼分辨
        const QString typeName = (spec.type == CalibrationBoardType::Chessboard)
                                     ? QStringLiteral("chessboard")
                                     : QStringLiteral("circles");
        const QString suggested = QStringLiteral("calibration_%1_%2x%3_%4mm_%5dpi.png")
                                      .arg(typeName)
                                      .arg(spec.squaresX)
                                      .arg(spec.squaresY)
                                      .arg(QString::number(spec.squareSizeMm, 'f', 1))
                                      .arg(spec.dpi);
        const QString lastDir =
            QSettings().value(QStringLiteral("tools/calibrationBoardDir")).toString();
        const QString startDir = lastDir.isEmpty() ? QDir::homePath() : lastDir;
        const QString path = QFileDialog::getSaveFileName(
            &dialog, tr("保存标定板"), QDir(startDir).filePath(suggested),
            tr("图片 (*.png *.bmp *.tif *.tiff);;所有文件 (*)"));
        if (path.isEmpty())
            return;

        if (!CalibrationBoard::save(path, spec, &error)) {
            errorLabel->setText(error);
            return;
        }

        QSettings().setValue(QStringLiteral("tools/calibrationBoardDir"),
                             QFileInfo(path).absolutePath());
        errorLabel->clear();
        statusLabel->setText(tr("已保存：%1").arg(QDir::toNativeSeparators(path)));
        logMessage(tr("标定板已保存：%1（%2 × %3 px）")
                       .arg(QDir::toNativeSeparators(path))
                       .arg(size.width())
                       .arg(size.height()));
    });

    refresh();   // 打开即显示默认参数（11×8、10 mm、300 DPI）的预览
    dialog.exec();
}

void MainWindow::openExceptionInfoDialog()
{
    // 采集诊断输入：全部为文本信息，不读取图像/模型/方案内容
    DiagnosticsInput input;
    input.appVersion = QStringLiteral(VFP_VERSION_DISPLAY);
    input.osVersion = QStringLiteral("%1（%2 %3）")
                          .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(),
                               QSysInfo::kernelVersion());
    input.qtVersion = QString::fromLatin1(qVersion());
    input.cpuArchitecture = QSysInfo::currentCpuArchitecture();
    input.appDir = QCoreApplication::applicationDirPath();
    input.schemePath = m_projectManager ? m_projectManager->lastFilePath() : QString();
    // 日志栏最多保留 2000 块：取末尾 200 行 / 2 万字符，足够定位又不至于让报告过大
    input.logTail = DiagnosticsCollector::tailLines(ui->logTextEdit->toPlainText(), 200, 20000);

    // HALCON 版本（运行时不可用时不阻断收集，报告标「未获取」）
    try {
        HTuple version;
        GetSystem("version", &version);
        if (version.Length() > 0)
            input.halconVersion = QString::fromStdString(version[0].S().Text());
    } catch (const HException &) {
    }

    const QString schemePath = m_projectManager ? m_projectManager->lastFilePath() : QString();
    const QString volumePath =
        schemePath.isEmpty() ? QDir::currentPath() : QFileInfo(schemePath).absolutePath();
    int cpuPercent = -1;
    int memoryPercent = -1;
    int diskPercent = -1;
    sampleResourceUsage(volumePath, &cpuPercent, &memoryPercent, &diskPercent);
    input.cpuPercent = cpuPercent;
    input.memoryPercent = memoryPercent;
    input.diskPercent = diskPercent;

    // 报告在打开时生成一次：预览与导出使用同一份快照，避免两次内容不一致
    const QString report = DiagnosticsCollector::buildReport(input);

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("exceptionInfoDialog"));
    dialog.setWindowTitle(tr("异常信息收集"));
    dialog.setMinimumSize(720, 520);
    auto *root = new QVBoxLayout(&dialog);

    auto *intro = new QLabel(
        tr("以下信息用于技术支持定位问题，不包含图像、模型或方案内容。可复制或导出为文本文件。"),
        &dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *preview = new QPlainTextEdit(&dialog);
    preview->setObjectName(QStringLiteral("exceptionInfoPreview"));
    preview->setReadOnly(true);
    preview->setPlainText(report);
    preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    root->addWidget(preview, 1);

    auto *statusLabel = new QLabel(&dialog);
    statusLabel->setStyleSheet(QStringLiteral("color:#2e7d32;"));
    statusLabel->setWordWrap(true);
    root->addWidget(statusLabel);

    auto *errorLabel = new QLabel(&dialog);
    errorLabel->setStyleSheet(QStringLiteral("color:#c62828;"));
    errorLabel->setWordWrap(true);
    root->addWidget(errorLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    QPushButton *copyButton =
        buttons->addButton(tr("复制到剪贴板"), QDialogButtonBox::ActionRole);
    QPushButton *exportButton = buttons->addButton(tr("导出…"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    root->addWidget(buttons);

    connect(copyButton, &QPushButton::clicked, &dialog,
            [this, report, statusLabel, errorLabel]() {
                QApplication::clipboard()->setText(report);
                errorLabel->clear();
                statusLabel->setText(tr("已复制到剪贴板"));
            });

    connect(exportButton, &QPushButton::clicked, &dialog,
            [this, &dialog, report, statusLabel, errorLabel]() {
                const QString lastDir =
                    QSettings().value(QStringLiteral("tools/diagnosticsDir")).toString();
                const QString startDir = lastDir.isEmpty() ? QDir::homePath() : lastDir;
                const QString suggested =
                    QStringLiteral("diagnostics_%1.txt")
                        .arg(QDateTime::currentDateTime().toString(
                            QStringLiteral("yyyyMMdd_HHmmss")));
                QString path = QFileDialog::getSaveFileName(
                    &dialog, tr("导出异常信息"), QDir(startDir).filePath(suggested),
                    tr("文本文件 (*.txt);;所有文件 (*)"));
                if (path.isEmpty())
                    return;

                QString error;
                if (!DiagnosticsCollector::writeReport(path, report, &error)) {
                    statusLabel->clear();
                    errorLabel->setText(error);
                    return;
                }
                if (QFileInfo(path).suffix().isEmpty())
                    path += QStringLiteral(".txt");   // 与 writeReport 的补全口径一致
                QSettings().setValue(QStringLiteral("tools/diagnosticsDir"),
                                     QFileInfo(path).absolutePath());
                errorLabel->clear();
                statusLabel->setText(tr("已导出：%1").arg(QDir::toNativeSeparators(path)));
                logMessage(tr("异常信息已导出：%1").arg(QDir::toNativeSeparators(path)));
            });

    dialog.exec();
}

bool MainWindow::isTextEditingFocus()
{
    // 判定集中在 FocusGuard（header-only，便于单测），这里只转调。
    return FocusGuard::isTextEditingFocus();
}

bool MainWindow::isNodeLive(NodeBase *node, FlowScene *executorScene) const
{
    if (!node)
        return false;
    for (FlowScene *scene : m_flowScenes) {
        if (scene && scene->nodes().contains(node))
            return true;
    }
    // 后台流程的执行器场景可能不在页签列表里，别误杀它的正常反馈
    if (executorScene && executorScene->nodes().contains(node))
        return true;
    return false;
}

void MainWindow::onNodeExecuted(NodeBase *executedNode, bool success)
{
    if (success) {
        ui->statusBar->showMessage(tr("节点 %1 执行成功").arg(executedNode->name()));

        // Get output data from the node (use port index 0)
        QSharedPointer<DataObject> outputData = executedNode->getOutputData(0);
        // B1：统一输出端口的主类型可能是 Measure/Number，图像在同一个对象里
        if (outputData && outputData->getHImage().IsInitialized()) {
            const HImage image = outputData->getHImage();
            if (image.IsInitialized()) {
                // 经 ImageDisplayController 显示：刷新图像**并**叠加图元（连续运行下叠加必须
                // 每节点更新，否则停留在上一个节点）。原内联实现只 setImage 不刷叠加，是半成品。
                m_imageDisplay->showImage(image, QString(), executedNode);
            }
        }
    } else {
        ui->statusBar->showMessage(tr("节点 %1 执行失败").arg(executedNode->name()));
    }
    
    // 更新参数面板，确保模块状态和其他输出参数正确显示
    if (m_selectedNode == executedNode) {
        QDockWidget *parameterDock = findChild<QDockWidget*>("parameterDock");
        if (parameterDock) {
            QWidget *parameterPanel = parameterDock->widget();
            if (parameterPanel) {
                // 查找参数面板中的所有子控件
                QList<QWidget*> childWidgets = parameterPanel->findChildren<QWidget*>();
                for (QWidget *childWidget : childWidgets) {
                    // 检查是否是标签页控件
                    QTabWidget *tabWidget = qobject_cast<QTabWidget*>(childWidget);
                    if (tabWidget) {
                        // 查找输出标签页
                        QWidget *outputTab = tabWidget->findChild<QWidget*>("outputTab");
                        if (outputTab) {
                            // 只更新输出参数面板，不更新输入参数和图像显示参数
                            executedNode->updateParamPanel(outputTab);
                            VFP_DEBUG << "执行后输出参数面板已更新";
                        }
                    }
                }
            }
        }
    }
}

void MainWindow::onExecutionStarted()
{
    // 项目/流程载入后再体检一次（2026-10-27 P3-6）：启动期节点名单未知，
    // 只在载入后才有意义
    if (m_runtimeView)
        m_runtimeView->setKnownNodeNames(allNodeFullNames());
    m_execStatus->onStarted();   // 状态栏 + 计时开始 + 按钮态
    refreshAllMvsPixelFormats();
    updateEditLockForCurrentScene();
    // 流程超时：从本次运行开始计时（0 = 不限）
    if (m_flowTimeoutTimer) {
        FlowScene *scene = currentFlowScene();
        const int timeoutMs = scene ? m_flowTimeouts.value(scene, 0) : 0;
        m_timeoutScene = timeoutMs > 0 ? scene : nullptr;
        if (timeoutMs > 0)
            m_flowTimeoutTimer->start(timeoutMs);
        else
            m_flowTimeoutTimer->stop();
    }
    // P1-2：新的一轮开始 → 当前结果清空，历史页保留上一轮
    for (ResultTablePanel *rt : resultPanels())
        rt->beginRunRound();
    // P2-4：新轮开始清空模块耗时，画布角标进入「运行中」
    m_nodeElapsed.clear();
    if (FlowCanvasView *view = canvasViewForScene(currentFlowScene())) {
        view->setFlowElapsedMs(-1);
        view->clearSelectedNodeElapsed();
    }
    // 运行态变化要重算菜单可用性（2026-10-29 r15 P2-13）：打包会改活方案路径
    applyPermissionRestrictions();
}

void MainWindow::onExecutionStopped()
{
    if (m_flowTimeoutTimer)
        m_flowTimeoutTimer->stop();
    m_timeoutScene = nullptr;
    // 采集策略·大文件存图：停止时把未满一批落盘，避免下一轮继续攒而丢在重启边界
    if (LargeFileImageStore::instance()->pendingCount() > 0)
        LargeFileImageStore::instance()->flush();
    m_execStatus->onStopped();   // 状态栏 + 耗时结算 + 按钮态
    refreshAllMvsPixelFormats();
    updateEditLockForCurrentScene();
    // 运行态变化要重算菜单可用性（2026-10-29 r15 P2-13）：打包会改活方案路径
    applyPermissionRestrictions();
    // P2-4：停止时也结算一次流程耗时（连续模式被手动停止时以实际耗时为准）
    if (FlowCanvasView *view = canvasViewForScene(currentFlowScene()))
        view->setFlowElapsedMs(m_execStatus->lastRunMs());
}

void MainWindow::onExecutionFinished()
{
    m_execStatus->onFinished();   // 状态栏 + 触发计数 + 耗时 + 按钮态（连续/触发模式判定在控制器内）
    fireSendEventsForRound();     // 每轮结束自动上报已启用的发送事件
    refreshAllMvsPixelFormats();
    updateEditLockForCurrentScene();
    // 正常跑完一轮只发 executionFinished、**不发** executionStopped（2026-10-29 r16 P2-13）：
    // 菜单原本只在 started/stopped 里重算，于是"点一次开始→跑完→菜单一直灰、画布已可编辑"。
    // 不能把 Finished 当成"已停止"（连续模式每轮都发、状态仍是 Running）——这里让
    // applyPermissionRestrictions 自己去读 allowsGraphEditing()，跑着就仍然是灰的。
    applyPermissionRestrictions();
    // P2-4：本轮结算后把流程耗时固定到画布角标
    if (FlowCanvasView *view = canvasViewForScene(currentFlowScene()))
        view->setFlowElapsedMs(m_execStatus->lastRunMs());
}

void MainWindow::checkYieldTarget(FlowExecutor *executor)
{
    if (!executor)
        return;

    // 目标与窗口来自配置（报表窗口里的"良率目标"写的就是这个键；0 = 未设目标 = 关闭）
    QSettings settings;
    YieldMonitor::Config config;
    config.targetPercent = settings.value(QStringLiteral("reporting/yieldTargetPercent"), 0.0).toDouble();
    config.windowRounds = settings.value(QStringLiteral("reporting/yieldWindowRounds"), 50).toInt();
    if (!config.isValid())
        return;

    // 节流：连续模式每秒可能几十轮，每轮都查库会拖住 UI 线程；报警不需要秒级实时
    constexpr qint64 kThrottleMs = 5000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_yieldLastEvalMs > 0 && now - m_yieldLastEvalMs < kThrottleMs)
        return;
    m_yieldLastEvalMs = now;

    const QString flowName = executor->flowName();
    YieldMonitor &monitor = m_yieldMonitors[flowName];
    monitor.setConfig(config);   // 目标改了立即生效（并重置闭锁，见 YieldMonitor::setConfig）

    // 取最近 24h 的记录，与报表同一个查询接口；limit 截断时是"最近的若干条"，够窗口用
    AppDatabase *db = AppDatabase::instance();
    const QList<InspectionRecord> records =
        db->queryResults(QDateTime::currentDateTime().addSecs(-24 * 3600), QDateTime::currentDateTime(),
                         5000);

    // 轮清单装配（双键过滤 + 时间升序重排 + 轮旗投影）在 YieldMonitor（U-172 微核入已面）
    const QList<bool> okFlags = YieldMonitor::collectRoundFlags(records, flowName);

    const YieldMonitor::Result result = monitor.evaluate(okFlags);
    if (!result.evaluated)
        return;   // 样本不足 / 已关闭：不打扰

    if (result.alarmRaised) {
        const QString msg = result.text();
        logMessage(QStringLiteral("⚠ %1：%2").arg(flowName, msg));
        ui->statusBar->showMessage(QStringLiteral("⚠ %1：%2").arg(flowName, msg), 15000);
        // 报警联动：写报警历史（可在「系统 → 报警历史」查看/追溯）
        db->addAlarm(flowName, QStringLiteral("Warning"), msg);
    } else if (result.alarmCleared) {
        const QString msg = result.text();
        logMessage(QStringLiteral("%1：%2").arg(flowName, msg));
        ui->statusBar->showMessage(QStringLiteral("%1：%2").arg(flowName, msg), 8000);
        db->addAlarm(flowName, QStringLiteral("Info"), msg);
    }
}

void MainWindow::fireSendEventsForRound()
{
    // 每轮结束 → 自动上报已启用的发送事件（未配置 / 被禁用 / 设备未连接都会静默跳过，
    // 返回值即"真正发出的条数"，无需在这里判断）。连续模式下本函数会被高频调用，
    // 故用一次性定时器把同一 UI 事件循环周期内的多轮合并为一次上报。
    if (m_sendEventFirePending)
        return;
    m_sendEventFirePending = true;
    QTimer::singleShot(0, this, [this]() {
        m_sendEventFirePending = false;
        // 每轮上报数据注入：{global.变量名} + {模块号.参数名}（与"变量引用"语法一致），
        // 发送事件的文本模板据此把本轮结果格式化进报文（对标 VM 的每轮结果上报）
        // 变量域两出口之一：上报 payload 在 RoundVariableFeed（{global.变量名} + {模块号.参数名}，
        // 与"变量引用"语法一致——发送事件的文本模板据此把本轮结果格式化进报文）
        CommunicationManager::instance()->fireEnabledSendEvents(RoundVariableFeed::roundPayload(
            GlobalVariableManager::instance()->variables(), m_lastModuleVars));
    });
}

void MainWindow::onExecutionError(const QString &error)
{
    m_execStatus->onError(error);   // 状态栏留痕（不弹模态框，理由见控制器注释）
    logMessage(QStringLiteral("执行错误: %1").arg(error));
    // 画布锁必须在这里也重算：onExecutionStopped / onExecutionFinished 都会算，但错误路径
    // 不一定走到 executionFinished（例如执行器内部提前停）。漏了这一行，画布会永久停在锁定态
    // ——现场表现为"算子拖不进去、双击能进但进去也不能动"（2026-10-24 现场反馈）。
    updateEditLockForCurrentScene();
}

QVector<OverlayShape> MainWindow::collectOverlayFromNode(NodeBase *node) const
{
    // 叠加规则（线/圆/模板/缺陷/卡尺 → OverlayShape）在 ImageDisplayController
    return m_imageDisplay->collectOverlayFromNode(node);
}

void MainWindow::startRoiPick(NodeBase *node)
{
    // 取点状态与 ROI 编辑开关在 ImageDisplayController；这里只做窗口级提示
    if (!m_imageDisplay->startRoiPick(node))
        return;
    ui->statusBar->showMessage(tr("请在图像上拖拽绘制搜索区域（右键取消）"), 5000);
}

bool MainWindow::prepareCanvasPick(NodeBase *node)
{
    if (!m_imageDisplay || !node)
        return false;
    // 模板匹配模块窗是纯参数窗（对齐 VM 4.4）：绘制落在主画布的图像显示上。
    // 先把图像显示拉回可见/停靠状态，否则「绘制」点下去只改了一个看不见的隐藏画布。
    if (ui->imageDock) {
        if (ui->imageDock->isFloating())
            onToggleImageFloat();
        ui->imageDock->show();
        ui->imageDock->raise();
    }
    if (!m_imageDisplay->displayNodeOutput(node)) {
        // 还没执行过 ⇒ 没有输出图：退回输入图，保证框选时有画面可点
        if (HalconNode *hn = qobject_cast<HalconNode *>(node)) {
            HImage img(hn->getInputImage());
            if (!img.IsInitialized())
                img = HImage(hn->getOutputImage());
            if (img.IsInitialized())
                m_imageDisplay->showImage(img, node->fullName(), node);
        }
    }
    if (!m_imageView || !m_imageView->hasImage()) {
        ui->statusBar->showMessage(tr("当前没有可绘制图像：请先执行图像源或载入图像"), 5000);
        return false;
    }
    return true;
}

void MainWindow::beginSearchRegionPick(NodeBase *node, bool polygon)
{
    if (!prepareCanvasPick(node))
        return;
    if (!m_imageDisplay->startSearchRegionPick(node, polygon)) {
        ui->statusBar->showMessage(tr("该模块没有可绘制的检测区域"), 4000);
        return;
    }
    ui->statusBar->showMessage(
        polygon ? tr("请在图像上逐点绘制检测区域：单击加点、双击完成、右键取消")
                : tr("请在图像上拖拽框定检测区域（右键取消）"),
        8000);
    // 模块窗保持在前：画布在右侧仍可点，画完能立刻看到参数回填
    if (QWidget *editor = m_editors.lookup(node)) {
        editor->raise();
        editor->activateWindow();
    }
}

void MainWindow::beginTemplateRoiPick(NodeBase *node, bool rotatedRect)
{
    if (!prepareCanvasPick(node))
        return;
    if (!m_imageDisplay->startTemplateRoiPick(node, rotatedRect)) {
        ui->statusBar->showMessage(tr("该模块没有可绘制的 ROI 区域"), 4000);
        return;
    }
    ui->statusBar->showMessage(tr("请在图像上框定 ROI 区域；框完在「特征模板」页点「训练模板」"), 8000);
    if (QWidget *editor = m_editors.lookup(node)) {
        editor->raise();
        editor->activateWindow();
    }
}

void MainWindow::beginMaskRegionPick(NodeBase *node)
{
    if (!prepareCanvasPick(node))
        return;
    if (!m_imageDisplay->startMaskRegionPick(node)) {
        ui->statusBar->showMessage(tr("该模块没有可绘制的屏蔽区"), 4000);
        return;
    }
    ui->statusBar->showMessage(
        tr("请在图像上逐点绘制屏蔽区：单击加点、双击完成、右键取消（可重复绘制多块）"), 8000);
    // 模块窗保持在前：画布在右侧仍可点，画完能立刻看到参数回填
    if (QWidget *editor = m_editors.lookup(node)) {
        editor->raise();
        editor->activateWindow();
    }
}

void MainWindow::beginCoordinatePick(NodeBase *node, int field)
{
    if (!prepareCanvasPick(node))
        return;
    if (!m_imageDisplay->startCoordinatePick(node, field)) {
        ui->statusBar->showMessage(tr("该模块没有可绘制的检测区域"), 4000);
        return;
    }
    ui->statusBar->showMessage(tr("请在图像上单击取点（写入检测区域参数）"), 8000);
    if (QWidget *editor = m_editors.lookup(node)) {
        editor->raise();
        editor->activateWindow();
    }
}

void MainWindow::beginGeometryPick(NodeBase *node)
{
    if (!prepareCanvasPick(node))
        return;
    if (!m_imageDisplay->startGeometryPick(node)) {
        ui->statusBar->showMessage(tr("该模块没有可绘制的 ROI 区域"), 4000);
        return;
    }
    ui->statusBar->showMessage(tr("请在图像上拖拽绘制 ROI（右键取消）"), 8000);
    if (QWidget *editor = m_editors.lookup(node)) {
        editor->raise();
        editor->activateWindow();
    }
}

void MainWindow::beginMaskEdit(NodeBase *node)
{
    if (!prepareCanvasPick(node))
        return;
    if (!m_imageDisplay->startMaskEdit(node)) {
        ui->statusBar->showMessage(tr("该模块不支持掩膜编辑"), 4000);
        return;
    }
    ui->statusBar->showMessage(tr("请在图像上涂抹掩膜：左键保留、右键擦除"), 8000);
    if (QWidget *editor = m_editors.lookup(node)) {
        editor->raise();
        editor->activateWindow();
    }
}

void MainWindow::handleMaskEdited()
{
    if (!m_imageDisplay || !m_imageDisplay->writeMaskBack())
        return;
    // 掩膜写入节点后，参数面板/画布状态不需要重建；执行时按新掩膜参与运算。
    if (m_selectedNode)
        showNodeParameters(m_selectedNode);
}

void MainWindow::handleRoiEdited(const RoiShape &shape)
{
    // 参数写回（各测量节点的行列/半径）在 ImageDisplayController；这里做提示与面板刷新
    NodeBase *node = m_imageDisplay->writeRoiToNode(shape);
    if (!node)
        return;
    ui->statusBar->showMessage(tr("ROI 已写入 %1 参数").arg(node->name()), 3000);
    // 刷新参数面板显示
    showNodeParameters(m_selectedNode);
    // 模块窗若开着：把刚写入的检测区域/几何回填到参数控件，避免窗里还是画之前的旧值
    if (QWidget *editorWidget = m_editors.lookup(node)) {
        if (auto *dlg = qobject_cast<ModuleEditorDialog *>(editorWidget)) {
            dlg->reloadFromNode();
            dlg->setResultOverlay(collectOverlayFromNode(node));
        }
    }
}

void MainWindow::openManualDialog()
{
    // 手册路径优先级：exe 同目录 docs/（现场可替换）> 仓库 docs/ > 嵌入资源（qrc 兜底）——
    // 候选链与首读者胜出语义在 ManualPathResolver（词条切片 B 帮族首抽，行为零变更钉）
    const QString markdown = ManualPathResolver::resolveMarkdown(
        ManualPathResolver::candidatePaths(QCoreApplication::applicationDirPath()));

    if (markdown.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("使用手册"),
                             QStringLiteral("未找到手册文件（docs/用户操作手册.md）。\n"
                                            "请确认部署包中包含 docs 目录。"));
        return;
    }

    // 使用手册非模态：可以边看手册边操作主界面（重复打开前置已有窗口）
    showAuxDialog(QStringLiteral("manual"), [this, markdown]() -> QDialog * {
        return new HelpDialog(markdown, this);
    });
}

void MainWindow::refreshAllMvsPixelFormats(){
    for (FlowScene *scene : m_flowScenes) {
        if (!scene) continue;
        for (NodeBase *node : scene->nodes()) {
            auto *mvs = qobject_cast<MvsImageSourceNode*>(node);
            if (mvs) {
                mvs->refreshPixelFormatEnabled();
            }
        }
    }
}

void MainWindow::updateEditLockForCurrentScene()
{
    int tabIdx = ui->flowTabs->currentIndex();
    if (tabIdx < 0 || tabIdx >= m_flowScenes.size()) return;

    FlowScene *scene = m_flowScenes[tabIdx];
    // S1 / Phase B 小步：改用执行器判据——"执行线程真在跑"或"暂停已受理但 worker 尚未停稳"才锁；
    // Idle/Stopped 与"已停稳的暂停"都允许编辑。旧的"连续模式一律锁"是钝器：连续模式空闲（未开始/
    // 已停止）时也编辑不了，而暂停恰恰是用户最想改图的时刻（恢复后新图下一轮生效）。
    scene->setEditLocked(SessionManager::instance()->canvasEditShouldBeLocked(
        m_executor ? !m_executor->allowsGraphEditing() : false));
}

void MainWindow::onCurrentTabChanged(int index)
{
    if (index < 0 || index >= m_flowScenes.size())
        return;

    // P2-5：切换流程后旧的「叠加图像」节点指针不再属于当前场景
    m_overlayNodes.clear();
    if (m_imageView)
        m_imageView->clearHImageOverlays();
    if (m_imageView2)
        m_imageView2->clearHImageOverlays();
    // 图像源下拉按当前场景重置（等新场景选中节点后再填充）
    refreshImageSourceCombo(nullptr);

    // 运行/暂停中禁止切换流程：执行器绑定的是正在执行的场景，重绑会让"本轮还在
    // 遍历旧场景节点、下一轮就去执行新场景"（现场表现为"切一下 tab 流程乱跑"），
    // 多流程并发下还可能让同一批节点被两个执行器同时执行。
    if (m_executor) {
        const ExecutionState st = m_executor->getState();
        if (st == ExecutionState::Running || st == ExecutionState::Paused) {
            ui->statusBar->showMessage(QStringLiteral("流程运行中，停止后才能切换流程"), 5000);
            const int cur = m_flowScenes.indexOf(m_executor->flowScene());
            if (cur >= 0 && cur != index) {
                QSignalBlocker blocker(ui->flowTabs);   // 回退页签，屏蔽信号防递归
                ui->flowTabs->setCurrentIndex(cur);
            }
            return;
        }
    }

    // 选区/参数/本地图/耗时角标都按“当前场景”语义：切页签后旧场景的节点不再是选中项。
    // 不清理会让 m_selectedNode / m_localImageNode 继续指向旧场景节点（旧场景随后被关闭时
    // 就是悬垂指针，现场表现为切页签后再点一下随机闪退）。
    m_selectedNode = nullptr;
    if (m_imageDisplay)
        m_imageDisplay->showNodeGeometry(nullptr);
    showNodeParameters(nullptr);
    refreshLocalImagePanel(nullptr);
    updateCanvasSelectionElapsed(nullptr);
    for (ResultTablePanel *rt : resultPanels()) {
        rt->setCurrentModule(-1);
        rt->setHelpContent(buildNodeHelpHtml(nullptr));
    }
    if (ResultTablePanel *history = m_auxPanels ? m_auxPanels->resultTablePanel() : nullptr)
        history->setHistoryOnlySelection(false);

    FlowScene *scene = m_flowScenes[index];
    // 一场景一执行器：切 tab 直接切换到该场景专属执行器（executorForScene 保证存在/自动创建），
    // 不再把同一个全局执行器重绑到新场景——这正是「两 tab 来回切 + 右键执行 → 两执行器并发
    // 跑同批节点」的根因（executorForScene 拉取策略与全局重绑互相打架）。
    m_executor = executorForScene(scene);
    if (m_executor)
        m_executor->setFlowScene(scene);

    // 性能面板跟随当前激活执行器重绑（⑤b）：此前只绑首个执行器、切 tab 不重绑，
    // 后台跑首流程会 clearStats 清掉正在看的统计；切 tab 后让面板显示当前流程的耗时。
    if (auto *pp = m_auxPanels ? m_auxPanels->performancePanel() : nullptr)
        pp->bindExecutor(m_executor);

    // 恢复该流程存储的运行模式
    if (m_flowModeCombo) {
        auto it = m_flowModes.constFind(scene);
        FlowMode mode = (it != m_flowModes.cend()) ? it.value() : FlowMode::SoftwareTrigger;
        {
            QSignalBlocker blocker(m_flowModeCombo);
            m_flowModeCombo->setCurrentIndex(static_cast<int>(mode));
        }
        m_executor->setFlowMode(mode);
        // 按钮态单一来源：更新 开始/停止/单次执行 可用性（连续/硬件已 startExecution → Running）
        if (m_execStatus)
            m_execStatus->updateButtons(m_executor ? m_executor->getState() : ExecutionState::Stopped);

        // 更新标签页标题，显示模式后缀
        const int mi = static_cast<int>(mode);
        const QString suffix = flowModeSuffix(mode);   // F-3：后缀映射单一来源
        // tab 标题用真实流程名，保持与触发路由名一致
        const QString base = (m_executor && !m_executor->flowName().isEmpty())
                                ? m_executor->flowName()
                                : QStringLiteral("流程 %1").arg(index + 1);
        ui->flowTabs->setTabText(index, base + suffix);

        // 状态栏提示
        static const char *modeNames[] = { "连续模式", "软触发模式", "硬触发模式" };
        const char *modeName = (mi >= 0 && mi < 3) ? modeNames[mi] : "未知";
        ui->statusBar->showMessage(QStringLiteral("流程 %1 — %2").arg(index + 1).arg(modeName), 5000);
    }
    updateEditLockForCurrentScene();
    syncCanvasLockAction();
    updateZoomStatus();   // P2：切流程后状态栏缩放到新画布的真实值
}



void MainWindow::setupToolLibrary()
{
    // 工具库由声明式注册表 NodeRegistry 自动生成
    VFP_DEBUG << "setupToolLibrary called (registry-driven)";

    // === 实装 4.4：模块箱形态改造（只做一次）===
    // .ui 里是「搜索框 + 树」的竖排；这里改成 左侧常驻分类图标竖条 + 右侧可折叠列表。
    // 收起态只占一条竖条（VM 图标条形态），点分类图标才展开对应模块列表。
    if (!m_toolBrowserPanel && ui->toolDockContents) {
        QLayout *oldLayout = ui->toolDockContents->layout();
        m_toolBrowserPanel = new QWidget(ui->toolDockContents);
        m_toolBrowserPanel->setObjectName(QStringLiteral("toolBrowserPanel"));
        auto *browserLayout = new QVBoxLayout(m_toolBrowserPanel);
        browserLayout->setContentsMargins(2, 2, 2, 2);
        browserLayout->setSpacing(2);

        if (oldLayout) {
            // takeAt 后控件仍在 toolDockContents 名下；addWidget 会把它重新挂到列表面板
            while (QLayoutItem *item = oldLayout->takeAt(0)) {
                if (QWidget *w = item->widget())
                    browserLayout->addWidget(w);
                delete item;
            }
            delete oldLayout;
            // ui->toolDockLayout 指向刚删掉的旧布局（悬挂），此后禁止再解引用
            ui->toolDockLayout = nullptr;
        } else {
            browserLayout->addWidget(ui->toolLibrary);
        }

        m_toolCategoryBar = new QWidget(ui->toolDockContents);
        m_toolCategoryBar->setObjectName(QStringLiteral("toolCategoryBar"));
        m_toolCategoryBar->setFixedWidth(42);
        m_toolCategoryBar->setStyleSheet(QStringLiteral(
            "QWidget#toolCategoryBar { background:#232333; border-right:1px solid #3a3a4e; }"));
        auto *catLayout = new QVBoxLayout(m_toolCategoryBar);
        catLayout->setContentsMargins(3, 4, 3, 4);
        catLayout->setSpacing(3);

        auto *dockRoot = new QHBoxLayout(ui->toolDockContents);
        dockRoot->setContentsMargins(0, 0, 0, 0);
        dockRoot->setSpacing(0);
        dockRoot->addWidget(m_toolCategoryBar, 0);
        dockRoot->addWidget(m_toolBrowserPanel, 1);
    }

    QVBoxLayout *browserLayout =
        m_toolBrowserPanel ? qobject_cast<QVBoxLayout *>(m_toolBrowserPanel->layout()) : nullptr;

    // 工具库顶部搜索框（仅创建一次；parent 归列表面板，随折叠一起隐藏）
    QLineEdit *searchBox = ui->toolDockContents->findChild<QLineEdit *>(QStringLiteral("toolSearchEdit"));
    if (!searchBox) {
        searchBox = new QLineEdit(m_toolBrowserPanel ? m_toolBrowserPanel : ui->toolDockContents);
        searchBox->setObjectName(QStringLiteral("toolSearchEdit"));
        searchBox->setPlaceholderText(QStringLiteral("搜索算子..."));
        searchBox->setClearButtonEnabled(true);
        searchBox->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2b2b3d; color: #e0e0e0;"
            "  border: 1px solid #4a6a9c; border-radius: 4px;"
            "  padding: 4px 8px; font-size: 13px;"
            "}"
            "QLineEdit:focus { border-color: #3a6ea5; }"
        );
        if (browserLayout)
            browserLayout->insertWidget(0, searchBox);
    }

    // 清空工具库
    ui->toolLibrary->clear();
    ui->toolLibrary->setHeaderHidden(true);
    // 宽度由列表面板/Dock 决定；不再用树自身的最小宽度把折叠态撑宽
    ui->toolLibrary->setMinimumWidth(0);
    ui->toolLibrary->setIndentation(20);
    ui->toolLibrary->setAnimated(true);
    ui->toolLibrary->setAlternatingRowColors(true);

    registerAllNodes();
    auto &registry = NodeRegistry::instance();

    // 按分组生成树
    for (const QString &group : registry.groups()) {
        QTreeWidgetItem *groupItem = new QTreeWidgetItem(ui->toolLibrary);
        groupItem->setText(0, group);

        for (const NodeRegistration *reg : registry.byGroup(group)) {
            auto *item = new QTreeWidgetItem(groupItem);
            item->setText(0, reg->displayName);
            item->setData(0, Qt::UserRole, reg->id);
            if (!reg->description.isEmpty()) {
                item->setToolTip(0, reg->description);
            }

            // 设置图标
            if (!reg->iconPath.isEmpty()) {
                QIcon icon(reg->iconPath);
                if (!icon.isNull()) {
                    item->setIcon(0, icon);
                }
            } else {
                // 根据节点类型生成默认图标
                QIcon icon = NodeIconPalette::categoryIcon(reg->category);
                item->setIcon(0, icon);
            }
        }
        groupItem->setExpanded(true);
    }

    // 搜索过滤：输入关键字时自动切回「全部模块」，否则搜索会被当前分类挡住
    connect(searchBox, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!text.trimmed().isEmpty() && !m_toolActiveGroup.isEmpty()) {
            m_toolActiveGroup.clear();
            for (QToolButton *button : m_toolCategoryButtons) {
                if (button)
                    button->setChecked(false);
            }
        }
        applyToolLibraryFilter();
    });

    // 连接工具库的点击信号
    connect(ui->toolLibrary, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int column) {
        QString nodeType = item->data(0, Qt::UserRole).toString();
        if (!nodeType.isEmpty()) {
            addNode(nodeType);
        }
    });

    // 启用拖拽功能
    ui->toolLibrary->setDragEnabled(true);

    // 为工具库添加拖拽事件过滤器
    ui->toolLibrary->viewport()->installEventFilter(this);

    // 分类图标竖条（全部模块 + 各分组），默认收起为竖条
    setupToolCategoryBar();
    setToolBrowserExpanded(false);
}

void MainWindow::setupToolCategoryBar()
{
    if (!m_toolCategoryBar)
        return;
    QVBoxLayout *catLayout = qobject_cast<QVBoxLayout *>(m_toolCategoryBar->layout());
    if (!catLayout)
        return;

    // 重建按钮（保留可重入：先清空旧按钮）
    m_toolCategoryButtons.clear();
    while (QLayoutItem *item = catLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    auto makeGroupButton = [&](const QString &group, const QString &label) {
        auto *button = new QToolButton(m_toolCategoryBar);
        button->setObjectName(group.isEmpty()
                                  ? QStringLiteral("toolCategoryAll")
                                  : QStringLiteral("toolCategory_%1").arg(group));
        button->setProperty("toolGroup", group);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(24, 24));
        button->setFixedSize(34, 34);
        button->setIcon(toolCategoryGlyph(group));
        button->setToolTip(label);
        button->setAccessibleName(label);
        button->setStyleSheet(QStringLiteral(
            "QToolButton { background:transparent; border:none; border-radius:4px; }"
            "QToolButton:hover { background:#2f2f45; }"
            "QToolButton:checked { background:#2d4a66; }"));
        catLayout->addWidget(button, 0, Qt::AlignHCenter);
        m_toolCategoryButtons.append(button);
        connect(button, &QToolButton::clicked, this, [this, group]() {
            // 同一分类再次点击 = 收起（VM 图标条行为）
            const bool sameGroup = m_toolBrowserExpanded && m_toolActiveGroup == group;
            setToolBrowserExpanded(!sameGroup, sameGroup ? QString() : group);
        });
        return button;
    };

    makeGroupButton(QString(), tr("全部模块"));
    auto &registry = NodeRegistry::instance();
    for (const QString &group : registry.groups())
        makeGroupButton(group, group);

    catLayout->addStretch(1);
}

void MainWindow::setToolBrowserExpanded(bool expanded, const QString &group)
{
    if (!ui || !ui->toolDock || !m_toolBrowserPanel)
        return;
    m_toolBrowserExpanded = expanded;
    if (expanded)
        m_toolActiveGroup = group;

    // 折叠时去掉 Dock 标题栏：VM 的模块箱竖条没有标题栏，默认标题栏会把最小宽度撑到 ~120px
    const bool hasCustomTitleBar = ui->toolDock->titleBarWidget() != nullptr;
    if (!expanded && !hasCustomTitleBar)
        ui->toolDock->setTitleBarWidget(new QWidget(ui->toolDock));
    else if (expanded && hasCustomTitleBar)
        ui->toolDock->setTitleBarWidget(nullptr);

    ui->toolDock->setMinimumWidth(expanded ? 200 : 48);
    m_toolBrowserPanel->setVisible(expanded);
    for (QToolButton *button : m_toolCategoryButtons) {
        if (button)
            button->setChecked(expanded && button->property("toolGroup").toString() == group);
    }
    applyToolLibraryFilter();

    if (expanded && !group.isEmpty() && ui->toolLibrary) {
        for (int i = 0; i < ui->toolLibrary->topLevelItemCount(); ++i) {
            QTreeWidgetItem *groupItem = ui->toolLibrary->topLevelItem(i);
            if (groupItem && groupItem->text(0) == group) {
                groupItem->setExpanded(true);
                ui->toolLibrary->scrollToItem(groupItem, QAbstractItemView::PositionAtTop);
                break;
            }
        }
    }
    // 图标竖条 ~42px；展开时给列表一个可用宽度（用户之后仍可拖分隔条改）
    resizeDocks({ui->toolDock}, {expanded ? 240 : 48}, Qt::Horizontal);
}

void MainWindow::applyToolLibraryFilter()
{
    if (!ui || !ui->toolLibrary)
        return;
    QLineEdit *searchBox =
        ui->toolDockContents->findChild<QLineEdit *>(QStringLiteral("toolSearchEdit"));
    const QString keyword = searchBox ? searchBox->text().trimmed() : QString();

    for (int gi = 0; gi < ui->toolLibrary->topLevelItemCount(); ++gi) {
        QTreeWidgetItem *group = ui->toolLibrary->topLevelItem(gi);
        const bool groupAllowed =
            m_toolActiveGroup.isEmpty() || group->text(0) == m_toolActiveGroup;
        int visible = 0;
        for (int ci = 0; ci < group->childCount(); ++ci) {
            QTreeWidgetItem *child = group->child(ci);
            // 中文名/英文名/别名走 NodeRegistry::matchesSearchKeyword 那一枚唯一口径；
            // 类型 id 那条"关键字至少两个字符"的规矩留在本处，Ctrl+F 那一侧不需要它。
            const NodeRegistration *reg =
                NodeRegistry::instance().findById(child->data(0, Qt::UserRole).toString());
            const bool match = keyword.isEmpty()
                               || child->text(0).contains(keyword, Qt::CaseInsensitive)
                               || (reg && NodeRegistry::matchesSearchKeyword(*reg, keyword))
                               || (keyword.size() >= 2
                                   && child->data(0, Qt::UserRole).toString().contains(
                                       keyword, Qt::CaseInsensitive));
            child->setHidden(!match);
            if (match) ++visible;
        }
        group->setHidden(!groupAllowed || visible == 0);
        if (groupAllowed && visible > 0)
            group->setExpanded(true);
    }
}


void MainWindow::setupFlowEditor()
{
    // 设置流程编辑器
    VFP_DEBUG << "setupFlowEditor called";
    
    // 为每个流程场景设置拖拽支持
    for (FlowScene *scene : m_flowScenes) {
        setupFlowSceneDragDrop(scene);
    }
    
    // 连接标签页切换信号
    connect(ui->flowTabs, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);
    // 流程页签按内容宽度排列（VM 流程栏页签只占文字宽度，单流程时不铺满整条栏）
    ui->flowTabs->tabBar()->setExpanding(false);
    // 切流程后状态栏提示按新场景的模块数刷新（onCurrentTabChanged 职责已重，不塞入其中）
    connect(ui->flowTabs, &QTabWidget::currentChanged, this, [this](int) { updateStatusHint(); });

    // 标签页红色 X 关闭按钮
    connect(ui->flowTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        onCloseFlowTab(index);
    });

    // P1-3：流程栏右键菜单（重命名/拷贝/删除/导出 Prc/运行间隔/NG 停止/超时）
    if (ui->flowTabs && ui->flowTabs->tabBar()) {
        ui->flowTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->flowTabs->tabBar(), &QTabBar::customContextMenuRequested,
                this, &MainWindow::showFlowTabContextMenu);
    }
}

void MainWindow::setupFlowSceneDragDrop(FlowScene *scene)
{
    // 设置流程场景的拖拽支持
    // FlowScene已经继承了QGraphicsScene的拖拽功能
    
    // 这里可以添加更多的拖拽相关设置
    // 例如：设置拖拽进入、拖拽移动、拖拽释放等事件处理
}

void MainWindow::setupRightPanel()
{
    VFP_DEBUG << "setupRightPanel called (dock mode)";

    if (ui->imageDock) {
        ui->imageDock->setMinimumWidth(380);
        ui->imageDock->setMinimumHeight(280);
    }
    if (ui->paramDock) {
        ui->paramDock->setMinimumWidth(280);
        ui->paramDock->setMinimumHeight(140);
    }
    if (m_imageView) {
        m_imageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_imageView->setMinimumSize(320, 240);
    }
}

void MainWindow::setupDockWidgets()
{
    VFP_DEBUG << "setupDockWidgets called";

    setDockOptions(AnimatedDocks | AllowNestedDocks | AllowTabbedDocks);
    setDockNestingEnabled(true);

    // === 流程编辑放到中央：上方为品牌 Logo 栏（FR4.4），右侧图像/参数可拖分隔条调整 ===
    if (ui->flowTabs) {
        ui->flowTabs->setParent(nullptr);

        auto *central = new QWidget(this);
        auto *centralLayout = new QVBoxLayout(central);
        centralLayout->setContentsMargins(0, 0, 0, 0);
        centralLayout->setSpacing(0);

        m_brandHeader = new BrandHeader(central);
        centralLayout->addWidget(m_brandHeader);
        centralLayout->addWidget(ui->flowTabs, 1);

        setCentralWidget(central);
    }
    if (ui->flowDock) {
        removeDockWidget(ui->flowDock);
        ui->flowDock->hide();
    }

    // === 工具箱 (左) ===
    if (ui->toolDock) {
        ui->toolDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
        ui->toolDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        ui->toolDock->setMinimumWidth(180);
        addDockWidget(Qt::LeftDockWidgetArea, ui->toolDock);
    }

    // === 图像显示 (右上，默认可拉大) ===
    if (ui->imageDock) {
        ui->imageDock->setFeatures(QDockWidget::DockWidgetClosable
                                   | QDockWidget::DockWidgetMovable
                                   | QDockWidget::DockWidgetFloatable);
        ui->imageDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        ui->imageDock->setMinimumWidth(380);
        ui->imageDock->setMinimumHeight(280);
        addDockWidget(Qt::RightDockWidgetArea, ui->imageDock);
    }

    // === 参数面板 (右侧，与图像同为「子页面」，FR4.3) ===
    if (ui->paramDock) {
        ui->paramDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
        ui->paramDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        ui->paramDock->setMinimumWidth(280);
        addDockWidget(Qt::RightDockWidgetArea, ui->paramDock);
        if (ui->imageDock) {
            // FR4.3：右侧区域包含「图像显示」与「参数配置」两个子页面，可切换显示。
            // 用选项卡式停靠实现：点击标签页即可切换；用户若更喜欢上下并排，
            // 直接拖动标签页拆出即可（Qt 原生行为），两种习惯都保留。
            tabifyDockWidget(ui->imageDock, ui->paramDock);
            ui->imageDock->raise();   // 默认停在图像页
        }
    }

    // === 底部结果显示区（VM 4.4：常驻右侧图像区下方，页签 历史结果 / 帮助）===
    // 结果面板一直由 AuxPanelManager 创建/喂数；这里提前建出并钉在右侧下方。
    // 去掉标题栏与关闭/浮动能力：VM 的结果区是主界面的一部分，不是可关的辅助浮窗。
    m_auxPanels->open(QStringLiteral("resultTable"));
    if (QDockWidget *resultDock = m_auxPanels->resultTableDock()) {
        resultDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        resultDock->setAllowedAreas(Qt::RightDockWidgetArea);
        if (!resultDock->titleBarWidget())
            resultDock->setTitleBarWidget(new QWidget(resultDock));
        resultDock->setMinimumHeight(120);
        resultDock->show();
    }

    // === 日志 (底) ===
    if (ui->logDock) {
        ui->logDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
        // VM 日志栏没有独立标题行：用空标题控件隐藏标题栏，保留「最近日志 + 展开」紧凑行；
        // 显隐仍可从「设置 → 面板显示」切换（关闭/浮动按钮不再是主入口）。
        ui->logDock->setTitleBarWidget(new QWidget(ui->logDock));
        ui->logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
        addDockWidget(Qt::BottomDockWidgetArea, ui->logDock);
    }

    // === 运行界面（自定义运行界面，运行时显示） ===
    {
        m_runtimeView = new RuntimeInterfaceView(this);
        m_runtimeViewDock = new QDockWidget(QStringLiteral("运行界面"), this);
        m_runtimeViewDock->setObjectName(QStringLiteral("runtimeViewDock"));
        m_runtimeViewDock->setWidget(m_runtimeView);
        m_runtimeViewDock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);
        m_runtimeViewDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        m_runtimeViewDock->hide();
        addDockWidget(Qt::RightDockWidgetArea, m_runtimeViewDock);

        // 运行模式是最大化浮窗，主界面菜单被挡住：标题栏放「返回设计模式」按钮，
        // 操作员/调试人员不必再靠任务栏切回主窗口找菜单。
        {
            auto *runtimeBar = new QWidget(m_runtimeViewDock);
            runtimeBar->setObjectName(QStringLiteral("runtimeDockTitleBar"));
            runtimeBar->setStyleSheet(QStringLiteral(
                "QWidget#runtimeDockTitleBar { background:#2d2d32; border-bottom:1px solid #1a1a1f; }"
                "QLabel { color:#d8d8d8; font-size:13px; }"
                "QToolButton { color:#d0d0d8; background:#3a5a7a; border:none;"
                "  border-radius:4px; padding:4px 10px; }"
                "QToolButton:hover { background:#4a6f95; }"));
            auto *barLay = new QHBoxLayout(runtimeBar);
            barLay->setContentsMargins(8, 3, 6, 3);
            auto *barTitle = new QLabel(tr("运行界面"), runtimeBar);
            auto *backBtn = new QToolButton(runtimeBar);
            backBtn->setText(tr("返回设计模式"));
            backBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
            backBtn->setToolTip(tr("退出运行模式，返回设计界面"));
            connect(backBtn, &QToolButton::clicked, this, &MainWindow::switchToDesignMode);
            barLay->addWidget(barTitle);
            barLay->addStretch();
            barLay->addWidget(backBtn);
            m_runtimeViewDock->setTitleBarWidget(runtimeBar);
        }

        // 载入默认布局
        loadRuntimeInterfaceLayout();

        // 运行视图按钮动作 → 流程控制
        connect(m_runtimeView, &RuntimeInterfaceView::actionTriggered, this,
                [this](const QString &actionId) {
            // 173 行拍板：闸开不发新跑——start/single/trigger 三枚起跑动作随闸收，stop 照常可用。
            if (actionId != QStringLiteral("stop")
                && !SessionManager::instance()->canRunFlow()) {
                logMessage(tr("写闸开启期间不受理新的运行：出厂默认口令仍在使用（改密后恢复）"));
                return;
            }
            if (actionId == QStringLiteral("start")) {
                if (m_executor->getFlowMode() == FlowMode::SoftwareTrigger)
                    m_executor->startExecution();
            } else if (actionId == QStringLiteral("stop")) {
                m_executor->stopExecution();
            } else if (actionId == QStringLiteral("single")) {
                if (m_executor->getFlowMode() == FlowMode::SoftwareTrigger)
                    m_executor->startExecution();
            } else if (actionId == QStringLiteral("trigger")) {
                // 触发当前流程（软触发一次）
                if (m_executor->getFlowMode() != FlowMode::HardwareTrigger)
                    m_executor->startExecution();
            }
            logMessage(QStringLiteral("运行界面按钮触发: %1").arg(actionId));
            });

            // 官方动作表（B2-2）：exec 类由上面的旧连接处理，这里只分派其余类型，
            // 否则同一行 exec 会被处理两次（起跑动作重复下发）。
            connect(m_runtimeView, &RuntimeInterfaceView::runtimeActionTriggered, this,
                [this](const QString &type, const QString &method, const QString &param) {
                    if (type == QLatin1String("exec")) return;
                    if (type == QLatin1String("reset")) {
                        m_executor->resetRuntimeStats();
                        logMessage(tr("运行界面按钮：已清零运行统计"));
                    } else if (type == QLatin1String("scheme")) {
                        if (method == QLatin1String("save")) onSaveProject();
                        else if (method == QLatin1String("load")) onLoadProject();
                        else logMessage(tr("运行界面按钮：方案动作「%1」未接线").arg(method));
                    } else if (type == QLatin1String("ui")) {
                        m_runtimeView->showPage(method.isEmpty() ? param : method);
                    } else if (type == QLatin1String("param")) {
                        // method = 目标节点全名，param = "参数键=值"
                        QString nodeName, paramKey;
                        QVariant value;
                        if (!parseParamActionTarget(method, param, &nodeName, &paramKey, &value)) {
                            logMessage(tr("运行界面参数动作格式错误：「%1」，应为 节点.参数=值")
                                           .arg(param));
                            return;
                        }
                        FlowScene *scene = currentFlowScene();
                        if (!scene) return;
                        for (NodeBase *n : scene->nodes()) {
                            if (n->fullName() != nodeName) continue;
                            n->setParam(paramKey, value);
                            logMessage(tr("运行界面参数动作：%1.%2 = %3")
                                           .arg(nodeName, paramKey, value.toString()));
                            return;
                        }
                        logMessage(tr("运行界面参数动作失败：找不到节点 %1").arg(nodeName));
                    }
                    });

                    // 参数设置控件（B3-1，官方 9.3）：运行界面直接改节点 int/float 参数
                    connect(m_runtimeView, &RuntimeInterfaceView::parameterChangeRequested, this,
                    [this](const QString &nodeName, const QString &paramKey, double value) {
                    if (nodeName.isEmpty() || paramKey.isEmpty()) return;
                    FlowScene *scene = currentFlowScene();
                    if (!scene) return;
                    for (NodeBase *n : scene->nodes()) {
                        if (n->fullName() != nodeName) continue;
                        n->setParam(paramKey, value);
                        logMessage(tr("运行界面修改参数：%1.%2 = %3")
                                       .arg(nodeName, paramKey)
                                       .arg(value, 0, 'g', 6));
                        return;
                    }
                    logMessage(tr("运行界面修改参数失败：找不到节点 %1").arg(nodeName));
                    });

                    // 开关控制（B3-2，官方 9.3）：bool 参数按真实类型写回
                    connect(m_runtimeView, &RuntimeInterfaceView::switchChangeRequested, this,
                    [this](const QString &nodeName, const QString &paramKey, bool enabled) {
                    if (nodeName.isEmpty() || paramKey.isEmpty()) return;
                    FlowScene *scene = currentFlowScene();
                    if (!scene) return;
                    for (NodeBase *n : scene->nodes()) {
                        if (n->fullName() != nodeName) continue;
                        n->setParam(paramKey, enabled);
                        logMessage(tr("运行界面开关：%1.%2 = %3")
                                       .arg(nodeName, paramKey,
                                            enabled ? tr("开") : tr("关")));
                        return;
                    }
                    logMessage(tr("运行界面开关失败：找不到节点 %1").arg(nodeName));
                    });

                    // 组合框（B3-3，官方 9.3）：在候选值之间切换，按字符串写入参数
                    connect(m_runtimeView, &RuntimeInterfaceView::comboChangeRequested, this,
                    [this](const QString &nodeName, const QString &paramKey, const QString &text) {
                    if (nodeName.isEmpty() || paramKey.isEmpty()) return;
                    FlowScene *scene = currentFlowScene();
                    if (!scene) return;
                    for (NodeBase *n : scene->nodes()) {
                        if (n->fullName() != nodeName) continue;
                        n->setParam(paramKey, text);
                        logMessage(tr("运行界面组合框：%1.%2 = %3")
                                       .arg(nodeName, paramKey, text));
                        return;
                    }
                    logMessage(tr("运行界面组合框失败：找不到节点 %1").arg(nodeName));
                    });

        // 全局变量变化 → 运行界面实时更新
        connect(GlobalVariableManager::instance(), &GlobalVariableManager::variableChanged,
                this, [](const QString &name, const QVariant &value) {
                    RuntimeInterfaceView::broadcastVariable(name, value);
                });

        // F11：运行界面全屏（dock 浮动 + 无边框全屏；现场大屏直接投全屏，再按 F11 还原）
        auto *runtimeFsShortcut = new QShortcut(QKeySequence(QStringLiteral("F11")), this);
        connect(runtimeFsShortcut, &QShortcut::activated, this, [this]() {
            if (!m_runtimeViewDock) return;
            if (!m_runtimeViewDock->isFullScreen()) {
                m_runtimeViewDock->setFloating(true);
                m_runtimeViewDock->showFullScreen();
                logMessage(QStringLiteral("运行界面已全屏（F11 还原）"));
            } else {
                m_runtimeViewDock->showNormal();
                m_runtimeViewDock->setFloating(false);
                logMessage(QStringLiteral("运行界面已还原"));
            }
        });
    }

    // Connect view menu toggles
    if (ui->actionToggleToolDock && ui->toolDock) {
        connect(ui->actionToggleToolDock, &QAction::toggled,
                ui->toolDock, &QDockWidget::setVisible);
        connect(ui->toolDock, &QDockWidget::visibilityChanged,
                ui->actionToggleToolDock, &QAction::setChecked);
    }
    if (ui->actionToggleParamDock && ui->paramDock) {
        connect(ui->actionToggleParamDock, &QAction::toggled,
                ui->paramDock, &QDockWidget::setVisible);
        connect(ui->paramDock, &QDockWidget::visibilityChanged,
                ui->actionToggleParamDock, &QAction::setChecked);
    }
    if (ui->actionToggleLogDock && ui->logDock) {
        connect(ui->actionToggleLogDock, &QAction::toggled,
                ui->logDock, &QDockWidget::setVisible);
        connect(ui->logDock, &QDockWidget::visibilityChanged,
                ui->actionToggleLogDock, &QAction::setChecked);
    }
    if (ui->actionToggleImageDock && ui->imageDock) {
        connect(ui->actionToggleImageDock, &QAction::toggled,
                ui->imageDock, &QDockWidget::setVisible);
        connect(ui->imageDock, &QDockWidget::visibilityChanged,
                ui->actionToggleImageDock, &QAction::setChecked);
    }
    if (ui->menuView) {
        auto *floatAct = ui->menuView->addAction(QStringLiteral("弹出/还原图像窗口"));
        connect(floatAct, &QAction::triggered, this, &MainWindow::onToggleImageFloat);
    }

    // Design/Runtime mode actions
    if (ui->actionDesignMode) {
        connect(ui->actionDesignMode, &QAction::triggered, this, &MainWindow::switchToDesignMode);
    }
    if (ui->actionRuntimeMode) {
        connect(ui->actionRuntimeMode, &QAction::triggered, this, &MainWindow::switchToRuntimeMode);
    }
    if (ui->actionRuntimeDesigner) {
        connect(ui->actionRuntimeDesigner, &QAction::triggered, this,
                &MainWindow::openRuntimeInterfaceDesigner);
    }

    // 失败中断策略（对标 VisionMaster：失败默认停止，可配置继续）
    if (ui->actionStopOnFailure) {
        QSettings settings;
        const bool stopOnFail =
            settings.value(QStringLiteral("flow/stopOnFailure"), true).toBool();
        ui->actionStopOnFailure->setChecked(stopOnFail);
        // 注意：setupDockWidgets 在 FlowExecutor 创建之前调用，此处判空；
        // 实际设置在 FlowExecutor 创建后应用（见构造函数"初始化执行器"处）
        if (m_executor)
            m_executor->setStopOnFailure(stopOnFail);
        connect(ui->actionStopOnFailure, &QAction::toggled, this, [this](bool on) {
            if (m_executor)
                m_executor->setStopOnFailure(on);
            QSettings s;
            s.setValue(QStringLiteral("flow/stopOnFailure"), on);
            logMessage(on ? QStringLiteral("已启用：节点失败时停止流程")
                          : QStringLiteral("已关闭：节点失败时流程继续执行"));
        });
    }

    // 恢复上次退出时的停靠布局（用户拖过的左右占比、面板显隐都要记住）。
    // 键名带版本：旧版本存过“流程区被挤成一条缝”的布局，不能让它覆盖本次修正后的默认比例。
    {
        QSettings settings;
        const QByteArray state =
            settings.value(QStringLiteral("ui/mainWindowState2")).toByteArray();
        if (!state.isEmpty() && restoreState(state))
            m_dockLayoutRestored = true;
    }
    // 启动默认必须能看到「工具箱」和「图像显示」：旧存档可能是用户把面板关掉后的状态，
    // 直接恢复会出现"打开软件只有一块空白画布"（现场反馈）。这两个面板固定 show。
    if (ui->toolDock)
        ui->toolDock->show();
    if (ui->imageDock)
        ui->imageDock->show();
    if (ui->paramDock)
        ui->paramDock->show();
    // 右侧是「图像显示 / 参数配置」两个子页签：默认必须停在图像显示页，
    // 否则 show(paramDock) 会把参数页抬到前面，现场看到的就是"图像显示没了"。
    if (ui->imageDock)
        ui->imageDock->raise();
    // 流程编辑已迁到中央控件：旧存档即使带 flowDock 也不让它重新露出来
    if (ui->flowDock)
        ui->flowDock->hide();

    // 恢复上次退出时打开的辅助面板（结果表 / 变量 / 性能统计 / 输出数据）
    restoreAuxPanelVisibility();

    // 结果区常驻：旧存档里的"关闭/浮动"位置不再生效（VM 结果区固定在图像区下方）
    if (QDockWidget *resultDock = m_auxPanels ? m_auxPanels->resultTableDock() : nullptr) {
        if (resultDock->isFloating())
            resultDock->setFloating(false);
        removeDockWidget(resultDock);
        addDockWidget(Qt::RightDockWidgetArea, resultDock);
        resultDock->show();
    }

    VFP_DEBUG << "setupDockWidgets completed";
}

void MainWindow::switchToDesignMode()
{
    VFP_DEBUG << "Switching to Design Mode";
    // 运行模式把「运行界面 / 图像显示」做成最大化浮窗；回设计态必须先把它们还原成停靠，
    // 否则设计界面会被已经最大化的浮窗挡住（旧实现只 hide 运行视图，图像浮窗仍盖在主窗口上）。
    if (m_runtimeViewDock) {
        if (m_runtimeViewDock->isFullScreen() || m_runtimeViewDock->isMaximized())
            m_runtimeViewDock->setWindowState(Qt::WindowNoState);
        m_runtimeViewDock->hide();
    }
    if (ui->imageDock) {
        if (ui->imageDock->isFullScreen() || ui->imageDock->isMaximized())
            ui->imageDock->setWindowState(Qt::WindowNoState);
        if (ui->imageDock->isFloating())
            ui->imageDock->setFloating(false);
    }
    m_runtimeViewVisible = false;

    // 显示所有 dock widgets（图像显示是设计态主视图之一，不能漏）
    if (ui->toolDock) ui->toolDock->show();
    if (ui->imageDock) ui->imageDock->show();
    if (ui->paramDock) ui->paramDock->show();
    if (ui->logDock) ui->logDock->show();
    // 右侧子页签默认停在图像显示（与启动默认一致）
    if (ui->imageDock) ui->imageDock->raise();

    // 解锁画布编辑
    for (FlowScene *scene : m_flowScenes) {
        if (scene) {
            QList<QGraphicsView*> views = scene->views();
            for (QGraphicsView *v : views) {
                v->setInteractive(true);
            }
        }
    }
    
    logMessage(QStringLiteral("已切换至设计模式"));
}

void MainWindow::switchToRuntimeMode()
{
    VFP_DEBUG << "Switching to Runtime Mode";
    // 隐藏工具箱 dock
    if (ui->toolDock) ui->toolDock->close();
    
    // 锁定画布编辑
    for (FlowScene *scene : m_flowScenes) {
        if (scene) {
            QList<QGraphicsView*> views = scene->views();
            for (QGraphicsView *v : views) {
                v->setInteractive(false);
            }
        }
    }

    // 若已配置自定义运行界面，则显示自定义运行界面（最大化浮窗），否则退回图像显示最大化
    loadRuntimeInterfaceLayout();
    if (m_runtimeViewDock && m_runtimeView && m_runtimeView->controlCount() > 0) {
        m_runtimeViewVisible = true;
        m_runtimeViewDock->show();
        m_runtimeViewDock->raise();
        if (!m_runtimeViewDock->isFloating())
            m_runtimeViewDock->setFloating(true);
        m_runtimeViewDock->setWindowState(m_runtimeViewDock->windowState() | Qt::WindowMaximized);
    } else {
        m_runtimeViewVisible = false;
        // 最大化图像显示
        if (ui->imageDock) {
            if (!ui->imageDock->isFloating()) {
                ui->imageDock->setFloating(true);
            }
            ui->imageDock->show();
            ui->imageDock->raise();
            ui->imageDock->setWindowState(ui->imageDock->windowState() | Qt::WindowMaximized);
        }
    }
    
    logMessage(QStringLiteral("已切换至运行模式"));
}

QStringList MainWindow::allNodeFullNames() const
{
    QStringList names;
    for (FlowScene *scene : m_flowScenes) {
        if (!scene) continue;
        for (NodeBase *node : scene->nodes()) {
            if (node) names.append(node->fullName());
        }
    }
    return names;
}

void MainWindow::openRuntimeInterfaceDesigner()
{
    // 非模态：开着设计器也能继续操作主界面；「应用并进入运行模式」后刷新运行视图
    showAuxDialog(QStringLiteral("runtimeDesigner"), [this]() -> QDialog * {
        auto *dlg = new RuntimeInterfaceDesigner(allNodeFullNames(), this);
        // 「应用并进入运行模式」必须真的切模式：先刷新运行视图布局，再进入运行模式显示它。
        connect(dlg, &QDialog::accepted, this, [this]() {
            loadRuntimeInterfaceLayout();
            switchToRuntimeMode();
        });
        return dlg;
    });
}

void MainWindow::loadRuntimeInterfaceLayout()
{
    if (!m_runtimeView) return;
    RuntimeInterface layout;
    if (layout.loadFromFile(RuntimeInterfaceDesigner::defaultLayoutPath())) {
        m_runtimeView->setInterface(layout);
        // 绑定失效红显（2026-10-27）：把当前流程节点名喂给视图做体检，
        // 失效绑定会以红字列在页面顶部（与设计器共用 staleBindings 判据）
        m_runtimeView->setKnownNodeNames(allNodeFullNames());

        // 参数设置控件回填当前值（2026-11-09）：不回填的话运行界面会显示下限，
        // 让人误以为参数被改成了那个值。回填走 QSignalBlocker，不会反向写节点。
        QHash<QString, double> seeds;
        QHash<QString, QString> combos;
        FlowScene *scene = currentFlowScene();
        if (scene) {
            for (const RuntimeInterfacePage &pg : layout.pages) {
                for (const RuntimeControl &c : pg.controls) {
                    if (c.bindKey.isEmpty() || c.paramKey.isEmpty()) continue;
                    const QString id = c.bindKey + QLatin1Char('|') + c.paramKey;
                    for (NodeBase *n : scene->nodes()) {
                        if (n->fullName() != c.bindKey) continue;
                        const QVariant cur = n->getParam(c.paramKey);
                        // 参数设置与开关控制按数值回填（bool 以 0/1 承载）
                        if (c.type == RuntimeControlType::ParamSetting
                            || c.type == RuntimeControlType::SwitchControl)
                            seeds.insert(id, cur.toDouble());
                        // 组合框按字符串回填，供 findText 选中
                        else if (c.type == RuntimeControlType::ComboBox)
                            combos.insert(id, cur.toString());
                        break;
                    }
                }
            }
        }
        m_runtimeView->setParameterValues(seeds);
        m_runtimeView->setComboSelections(combos);
    }
}

void MainWindow::setupSchemeMenu()
{
    // 设置方案/文件菜单
    VFP_DEBUG << "setupSchemeMenu called";
    
    // 连接方案保存和加载动作
    connect(ui->actionSaveScheme, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(ui->actionOpenScheme, &QAction::triggered, this, &MainWindow::onLoadProject);
    
    // 连接文件菜单动作
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::onNewProject);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::onSaveProject);
    connect(ui->actionLoad, &QAction::triggered, this, &MainWindow::onLoadProject);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::onExit);
    
    // 连接编辑菜单动作
    connect(ui->actionAddFlow, &QAction::triggered, this, &MainWindow::onAddFlowTab);
    connect(ui->actionDeleteFlow, &QAction::triggered, this, [this]() {
        int currentIndex = ui->flowTabs->currentIndex();
        onCloseFlowTab(currentIndex);
    });

    // ── P1-6：文件菜单条目在这里只创建、由 reorganizeMenuBar() 统一按官方顺序重挂 ──
    // 旧实现用 addAction 追加，实际显示顺序是「退出 → 导出加密 → 最近打开 → 另存为 → 导入」；
    // 其中「导出加密」还和 m_actionExportEncrypted 重复调用了同一个槽。
    // 文案与 VM 4.4 实际菜单（2026-11-07 逐屏截图）一致
    m_actionSaveAs = new QAction(tr("方案另存为"), this);
    connect(m_actionSaveAs, &QAction::triggered, this, [this]() { saveProjectInteractively(); });
    m_actionImportFlow = new QAction(tr("导入流程"), this);
    connect(m_actionImportFlow, &QAction::triggered, this, &MainWindow::onImportSnippet);
    m_actionOpenExample = new QAction(tr("打开示例"), this);
    m_actionOpenExample->setStatusTip(tr("打开安装目录 examples/ 下的示例方案"));
    connect(m_actionOpenExample, &QAction::triggered, this, &MainWindow::onOpenExampleProject);

    // P0-1：最近删除恢复入口放在「编辑」菜单
    if (ui->menuEdit) {
        ui->menuEdit->addSeparator();
        m_restoreDeletedFlowAction = ui->menuEdit->addAction(QStringLiteral("恢复最近删除的流程"));
        m_restoreDeletedFlowAction->setEnabled(!m_lastDeletedFlowJson.isEmpty());
        connect(m_restoreDeletedFlowAction, &QAction::triggered,
                this, &MainWindow::restoreLastDeletedFlow);
        // 撤销/重做在编辑菜单也应有入口（工具栏按钮是主入口）
        if (m_undoAction)
            ui->menuEdit->addAction(m_undoAction);
        if (m_redoAction)
            ui->menuEdit->addAction(m_redoAction);
    }
}

void MainWindow::reorganizeMenuBar()
{
    if (!ui || !ui->menuBar)
        return;

    // 实装 4.4 菜单栏：文件 / 设置 / 工具 / 系统 / 帮助。
    // 编辑、视图、通讯不再作为顶层：编辑入口收进「工具 → 编辑」，视图项分流到设置/工具，
    // 通讯全部收进「系统 → 通信管理」。
    //
    // QAction 可以同时属于多个 QMenu：搬到新菜单后必须从旧菜单摘除，否则同一个动作会在
    // 「系统」和「工具」里重复出现（旧实现只 add 不 remove，实际菜单里全是重复项）。
    auto moveAction = [](QMenu *target, QAction *action) {
        if (!target || !action)
            return;
        const QList<QObject *> owners = action->associatedObjects();
        for (QObject *obj : owners) {
            if (QMenu *menu = qobject_cast<QMenu *>(obj); menu && menu != target)
                menu->removeAction(action);
        }
        if (!target->actions().contains(action))
            target->addAction(action);
    };
    // 搬走动作后原菜单会留下孤立分隔符（开头/连续/末尾），统一清理，
    // 否则「系统」会变成一串空行。removeAction 不删除分隔符对象，只让它不再显示。
    auto tidySeparators = [](QMenu *menu) {
        if (!menu)
            return;
        QAction *previous = nullptr;
        const QList<QAction *> actions = menu->actions();
        for (QAction *action : actions) {
            if (action->isSeparator() && (!previous || previous->isSeparator()))
                menu->removeAction(action);
            else
                previous = action;
        }
        const QList<QAction *> remaining = menu->actions();
        if (!remaining.isEmpty() && remaining.last()->isSeparator())
            menu->removeAction(remaining.last());
    };

    QMenu *settingsMenu = new QMenu(tr("设置"), this);
    QMenu *toolsMenu = new QMenu(tr("工具"), this);

    // ── 文件（官方顺序：新建 / 打开 / 最近打开 / 打开示例 / 保存 / 另存为 /
    //    导出加密 / 导出算子代码 / 导入流程 / 退出）──
    // 旧条目先整体摘除（removeAction 不删除 QAction），再按官方顺序重挂。
    if (ui->menuFile) {
        QMenu *file = ui->menuFile;
        const QList<QAction *> oldActions = file->actions();
        for (QAction *action : oldActions)
            file->removeAction(action);

        file->addAction(ui->actionNew);
        file->addAction(ui->actionOpenScheme);
        if (m_recentFiles && m_recentFiles->menu())
            file->addMenu(m_recentFiles->menu());
        if (m_actionOpenExample) {
            // 没装示例包时置灰，而不是留一个点了只会报错的死入口
            m_actionOpenExample->setEnabled(!bundledExamplePath().isEmpty());
            file->addAction(m_actionOpenExample);
        }
        file->addAction(ui->actionSaveScheme);
        if (m_actionSaveAs)
            file->addAction(m_actionSaveAs);
        if (m_actionExportEncrypted)
            file->addAction(m_actionExportEncrypted);
        moveAction(file, ui->actionCodeExport);
        if (m_actionImportFlow)
            file->addAction(m_actionImportFlow);
        file->addSeparator();
        file->addAction(ui->actionExit);
    }

    // ── 设置（官方顺序：权限设置 / 软件设置 / 方案设置 / 运行策略 / 采集策略）──
    moveAction(settingsMenu, ui->actionUserManagement);
    settingsMenu->addSeparator();
    QAction *softwareSettingsAct = settingsMenu->addAction(tr("软件设置…"));
    softwareSettingsAct->setObjectName(QStringLiteral("actionSoftwareSettings"));
    connect(softwareSettingsAct, &QAction::triggered, this, &MainWindow::openSoftwareSettings);
    QAction *schemeSettingsAct = settingsMenu->addAction(tr("方案设置…"));
    schemeSettingsAct->setObjectName(QStringLiteral("actionSchemeSettings"));
    connect(schemeSettingsAct, &QAction::triggered, this, &MainWindow::openSchemeSettings);
    settingsMenu->addSeparator();
    QMenu *runPolicyMenu = settingsMenu->addMenu(tr("运行策略"));

    // 面板显隐是 VM 设置菜单之外的补充入口，放在官方条目之后（见本函数末段）

    // 视图：设计/运行模式与失败停止 → 设置 → 运行策略
    moveAction(runPolicyMenu, ui->actionDesignMode);
    moveAction(runPolicyMenu, ui->actionRuntimeMode);
    moveAction(runPolicyMenu, ui->actionStopOnFailure);
    // 运行策略（VM 4.4 手册 11.4）：运行模式=普通/诊断；策略模式固定为耗时稳定
    runPolicyMenu->addSeparator();
    QAction *diagAct = runPolicyMenu->addAction(tr("资源诊断模式（超限告警）"));
    diagAct->setObjectName(QStringLiteral("actionResourceDiagnosticMode"));
    diagAct->setCheckable(true);
    diagAct->setChecked(m_resourceDiagnosticMode);
    connect(diagAct, &QAction::toggled, this, [this](bool on) {
        m_resourceDiagnosticMode = on;
        QSettings().setValue(QStringLiteral("flow/resourceDiagnosticMode"), on);
        logMessage(on ? tr("已切换诊断模式：CPU/内存/硬盘占用过高时告警")
                      : tr("已切换普通模式：资源占用过高时不告警"));
    });
    QAction *strategyAct =
        runPolicyMenu->addAction(tr("策略模式：耗时稳定（每流程单线程串行执行）"));
    strategyAct->setEnabled(false);

    // ── 工具（官方：标定配置引导 / 标定板生成 / 异常信息收集 / 参数查找工具）──
    QAction *wizardAct = toolsMenu->addAction(tr("标定配置引导…"));
    wizardAct->setObjectName(QStringLiteral("actionCalibrationWizard"));
    connect(wizardAct, &QAction::triggered, this,
            &MainWindow::openCalibrationWizardDialog);
    QAction *calibrationBoardAct = toolsMenu->addAction(tr("标定板生成…"));
    calibrationBoardAct->setObjectName(QStringLiteral("actionCalibrationBoard"));
    connect(calibrationBoardAct, &QAction::triggered, this,
            &MainWindow::openCalibrationBoardDialog);
    QAction *exceptionInfoAct = toolsMenu->addAction(tr("异常信息收集…"));
    exceptionInfoAct->setObjectName(QStringLiteral("actionExceptionInfo"));
    connect(exceptionInfoAct, &QAction::triggered, this,
            &MainWindow::openExceptionInfoDialog);
    moveAction(toolsMenu, ui->actionParameterSearch);

    // 编辑：撤销/重做、分组、子流程、片段导入导出、恢复删除流程、流程增删
    m_editMenu = toolsMenu->addMenu(tr("编辑"));
    if (ui->menuEdit) {
        const QList<QAction *> editActions = ui->menuEdit->actions();
        for (QAction *action : editActions)
            moveAction(m_editMenu, action);
    }

    // 视图剩余动作（运行界面设计 / 结果表 / 统计报表）→ 工具
    if (ui->menuView) {
        const QList<QAction *> viewActions = ui->menuView->actions();
        for (QAction *action : viewActions)
            moveAction(toolsMenu, action);
    }

    // 全局触发/变量与数据类入口归「工具」（VM 的全局触发/变量在工具条上，菜单无固定层级）
    // 注意：actionReport（检测报表）留在「系统」的数据类入口里；
    // 视图遗留的 actionStatisticsReport 才是「工具 → 统计报表」，两者不能同名堆在一起。
    QAction *toolActions[] = {
        ui->actionGlobalTrigger, ui->actionGlobalVariables, ui->actionRecipeManager,
        ui->actionAlarmHistory, ui->actionInspectionResults
    };
    for (QAction *action : toolActions)
        moveAction(toolsMenu, action);
    // 模型打包（父目录/依赖收进方案）也是工程工具，不再留在「文件」
    moveAction(toolsMenu, m_packModelsAct);

    // 工具：算子搜索 / 性能分析
    QAction *nodeSearchAct = toolsMenu->addAction(tr("算子搜索…"));
    nodeSearchAct->setObjectName(QStringLiteral("actionNodeSearch"));
    connect(nodeSearchAct, &QAction::triggered, this, &MainWindow::onOpenNodeSearch);
    QAction *perfAct = toolsMenu->addAction(tr("性能分析"));
    perfAct->setObjectName(QStringLiteral("actionPerformancePanel"));
    connect(perfAct, &QAction::triggered, this, &MainWindow::onOpenPerformancePanel);

    // 设置：采集策略（VM 4.4 手册 11.5：大文件存图）
    m_actionAcquisitionStrategy = settingsMenu->addAction(tr("采集策略…"));
    m_actionAcquisitionStrategy->setObjectName(QStringLiteral("actionAcquisitionStrategy"));
    connect(m_actionAcquisitionStrategy, &QAction::triggered, this, [this]() {
        showAuxDialog(QStringLiteral("largeFileStore"), [this]() -> QDialog * {
            return new LargeFileStoreDialog(this);
        });
    });

    // 设置：综合配置（相机/通讯/触发/用户/报表的总入口）
    QAction *comprehensiveAct = settingsMenu->addAction(tr("综合配置"));
    comprehensiveAct->setObjectName(QStringLiteral("actionComprehensiveConfig"));
    connect(comprehensiveAct, &QAction::triggered, this,
            &MainWindow::openComprehensiveConfig);

    // 面板显隐（补充入口；VM 设置菜单本身没有此项，故放在官方条目之后）
    QMenu *panelMenu = settingsMenu->addMenu(tr("面板显示"));
    moveAction(panelMenu, ui->actionToggleToolDock);
    moveAction(panelMenu, ui->actionToggleParamDock);
    moveAction(panelMenu, ui->actionToggleImageDock);
    moveAction(panelMenu, ui->actionToggleLogDock);

    // ── 系统（官方顺序：日志 / 通信管理 / 相机管理）──
    // 通信管理子菜单：原顶层「通讯」全部收进来
    QMenu *commMenu = ui->menuSystem ? ui->menuSystem->addMenu(tr("通信管理")) : nullptr;
    if (commMenu && ui->menuCommunication) {
        const QList<QAction *> commActions = ui->menuCommunication->actions();
        for (QAction *action : commActions)
            moveAction(commMenu, action);
    }
    // 系统菜单按官方顺序重挂：日志 → 通信管理 → 相机管理；其余系统级入口（流程变量/
    // 改密/AI 标注与训练等）保留在分隔线之后，不丢功能。
    if (ui->menuSystem) {
        QMenu *system = ui->menuSystem;
        const QList<QAction *> remaining = system->actions();
        for (QAction *action : remaining)
            system->removeAction(action);

        system->addAction(ui->actionOperationLog);
        if (commMenu)
            system->addMenu(commMenu);
        system->addAction(ui->actionCameraConfig);

        QList<QAction *> extras;
        for (QAction *action : remaining) {
            if (action->isSeparator() || action == ui->actionOperationLog
                || action == ui->actionCameraConfig
                || (commMenu && action == commMenu->menuAction()))
                continue;
            extras << action;
        }
        if (!extras.isEmpty()) {
            system->addSeparator();
            for (QAction *action : extras)
                system->addAction(action);
        }
    }

    // 帮助：语言子菜单（实装 4.4 的“语言”位于帮助菜单内）
    if (m_helpMenu) {
        m_helpMenu->setTitle(tr("帮助"));
        QMenu *langMenu = new QMenu(tr("语言"), m_helpMenu);
        QAction *actZh = langMenu->addAction(tr("简体中文"));
        QAction *actEn = langMenu->addAction(tr("English"));
        actZh->setCheckable(true);
        actEn->setCheckable(true);
        const I18n::Language cur = I18n::currentLanguage();
        actZh->setChecked(cur == I18n::Language::Chinese);
        actEn->setChecked(cur == I18n::Language::English);
        connect(actZh, &QAction::triggered, this,
                [this] { switchLanguage(I18n::Language::Chinese); });
        connect(actEn, &QAction::triggered, this,
                [this] { switchLanguage(I18n::Language::English); });
        const QList<QAction *> helpActions = m_helpMenu->actions();
        if (!helpActions.isEmpty())
            m_helpMenu->insertMenu(helpActions.first(), langMenu);
        else
            m_helpMenu->addMenu(langMenu);
    }

    // 搬移完成后清理孤立分隔符（系统/工具/编辑是"接收搬入项"的菜单）
    tidySeparators(ui->menuSystem);
    tidySeparators(toolsMenu);
    tidySeparators(m_editMenu);

    // 顶层重排：摘掉 编辑/视图/通讯，插入 设置/工具（在 系统 之前）
    if (ui->menuEdit)
        ui->menuBar->removeAction(ui->menuEdit->menuAction());
    if (ui->menuView)
        ui->menuBar->removeAction(ui->menuView->menuAction());
    if (ui->menuCommunication)
        ui->menuBar->removeAction(ui->menuCommunication->menuAction());
    if (ui->menuSystem) {
        ui->menuBar->insertMenu(ui->menuSystem->menuAction(), settingsMenu);
        ui->menuBar->insertMenu(ui->menuSystem->menuAction(), toolsMenu);
    }
}

void MainWindow::showAuxDialog(const QString &key, const std::function<QDialog *()> &create)
{
    // 统一委托 AuxPanelManager：同 key 复用、关闭即销毁（非模态，不阻塞主界面）
    m_auxPanels->showDialog(key, create);
}

void MainWindow::setupCommunicationMenu()
{
    VFP_DEBUG << "setupCommunicationMenu called";

    if (!ui->menuCommunication) return;

    // 非模态：打开通讯管理/监视后主界面仍可正常操作；重复点击前置已有窗口而不是再开一个。
    // 动作 → 对话框键的路由经 AuxDialogMenuRouter（词条切片 B 第二抽）：四枚设备/事件
    // 动作同键 commManager（共用一个管理器现窗），只读监视另键 commMonitor。
    auto showCommManager = [this]() -> QDialog * { return new CommunicationManagerDialog(this); };
    m_menuRouter.toDialog(ui->actionCommDevice, this, QStringLiteral("commManager"),
                          showCommManager);
    m_menuRouter.toDialog(ui->actionCommReceiveEvent, this, QStringLiteral("commManager"),
                          showCommManager);
    m_menuRouter.toDialog(ui->actionCommSendEvent, this, QStringLiteral("commManager"),
                          showCommManager);
    m_menuRouter.toDialog(ui->actionCommHeartbeat, this, QStringLiteral("commManager"),
                          showCommManager);
    m_menuRouter.toDialog(ui->actionCommMonitor, this, QStringLiteral("commMonitor"),
                          [this]() -> QDialog * { return new CommMonitorDialog(this); });
}

void MainWindow::setupSystemMenu()
{
    VFP_DEBUG << "setupSystemMenu called";

    if (!ui->menuSystem) return;

    // 动作 → 对话框键的路由经 AuxDialogMenuRouter（词条切片 B 第二抽）：固定键直走过
    // toDialog；场景依赖两条（流程变量 / 配方）经 toFlowSceneDialog——场景在触发当下
    // 经 currentFlowScene() 现取（与原内联两行同形：idx 越界两向，ui 空卫为严格安全超集）；
    // 多场景两条（参数搜索 / 代码导出）经 toScenesDialog——列表触发当下快照。
    m_menuRouter.toDialog(ui->actionGlobalTrigger, this, QStringLiteral("globalTrigger"),
                          [this]() -> QDialog * { return new GlobalTriggerDialog(this); });

    m_menuRouter.toDialog(ui->actionGlobalVariables, this, QStringLiteral("globalVariables"),
                          [this]() -> QDialog * { return new GlobalVariableDialog(this); });

    if (ui->menuSystem) {
        QAction *flowVarAct = ui->menuSystem->addAction(QStringLiteral("流程变量 / Fixture…"));
        // 缺场景原体静默早退（无声无提示）：onMissing 给空操作，语义原样入路由面
        m_menuRouter.toFlowSceneDialog(
            flowVarAct, this, QStringLiteral("flowVariable"),
            [this]() { return currentFlowScene(); }, [] {},
            [this](FlowScene *scene) -> QDialog * { return new FlowVariableDialog(scene, this); });
    }

    if (ui->menuSystem) {
        QAction *retireAct = ui->menuSystem->addAction(
            QStringLiteral("修改管理员口令（解除写操作禁止）…"));
        // 该项刻意不受角色/写闸限制：它就是解除闸的唯一入口；
        // 而改密前必须填对当前出厂口令，非持有者点了也改不动任何东西。
        retireAct->setEnabled(true);
        m_menuRouter.direct(retireAct, this, [this]() { enforceFactoryPasswordPolicy(); });
    }

    if (ui->menuSystem) {
        // 脚本安全：执行前确认（2026-10-25 现场反馈"每次都要手动点确认"）。
        // 策略本身是对的（默认开启、会话内同内容只问一次、改内容重问），但界面此前**没有
        // 任何开关**——脚本安全策略头注释写明"可由配置关闭"，实际只能改注册表。
        // 这里把配置接到菜单上：勾掉即直接执行（风险自负），勾上恢复确认。
        //
        // 2026-10-25 复核对本开关的三条要求，一并收在这里：
        //   ① 权限收口：属整机安全配置，按 Engineer 及以上（见 applyPermissionRestrictions）；
        //   ② 关闭要二次确认：一次误点会让整机的脚本都不再确认；
        //   ③ 关闭状态要留痕：落 operation_logs + 启动时状态栏提示（只有勾选看不出关着）。
        ScriptSecurityPolicy &policy = ScriptSecurityPolicy::instance();
        m_confirmScriptAct = ui->menuSystem->addAction(
            QStringLiteral("脚本安全：执行前确认脚本（取消勾选则直接执行）"));
        m_confirmScriptAct->setCheckable(true);
        // 计算后端（2026-10-27）：T-API 透明加速，现场要能关掉（驱动异常回退）并看得见。
        // 这是性能设置不是安全设置，不落审计，但切换后状态栏显示当前实际后端。
        {
            auto *backendMenu = ui->menuSystem->addMenu(QStringLiteral("计算后端"));
            auto *grp = new QActionGroup(backendMenu);
            grp->setExclusive(true);
            const auto mode = ComputeBackend::instance().mode();
            struct Item { const char *text; ComputeBackend::Mode m; };
            const Item items[] = {
                {"自动（有 OpenCL 就用）", ComputeBackend::Mode::Auto},
                {"强制 OpenCL", ComputeBackend::Mode::OpenCL},
                {"仅 CPU", ComputeBackend::Mode::Cpu},
            };
            for (const Item &it : items) {
                auto *act = backendMenu->addAction(QString::fromUtf8(it.text));
                act->setCheckable(true);
                act->setActionGroup(grp);
                act->setChecked(it.m == mode);
                connect(act, &QAction::triggered, this, [this, it]() {
                    ComputeBackend::instance().setMode(it.m);
                    statusBar()->showMessage(
                        QStringLiteral("计算后端：%1").arg(ComputeBackend::instance().describe()), 8000);
                });
            }
        }
        // 特征库管理（2026-10-27）：现场要看得到库里有什么——浏览/预览/删除/清空
        connect(ui->menuSystem->addAction(QStringLiteral("特征库管理…")), &QAction::triggered,
                this, [this]() {
                    FeatureLibraryDialog dlg(this);
                    dlg.exec();
                });
        // 训练向导（2026-10-27）：数据集装配 → 训练 → 评估 → 发布，一条链走完
        connect(ui->menuSystem->addAction(QStringLiteral("训练向导…")), &QAction::triggered,
                this, [this]() {
                    TrainingWizardDialog dlg(this);
                    // 一键发布为算子（2026-10-27）：把"插预设节点"能力注入向导；
                    // 走 addNode（有画布锁检查与状态提示），再把参数灌到刚创建的节点。
                    dlg.insertNode = [this](const QString &nodeType, const QJsonObject &params) -> bool {
                        FlowScene *scene = nullptr;
                        const int idx = ui->flowTabs ? ui->flowTabs->currentIndex() : -1;
                        if (idx >= 0 && idx < m_flowScenes.size())
                            scene = m_flowScenes[idx];
                        const int before = scene ? scene->nodes().size() : 0;
                        addNode(nodeType);
                        if (!scene || scene->nodes().size() <= before) {
                            statusBar()->showMessage(QStringLiteral("插入节点失败（画布已锁定？）"), 8000);
                            return false;
                        }
                        NodeBase *node = scene->nodes().last();
                        for (auto it = params.begin(); it != params.end(); ++it)
                            node->setParam(it.key(), it.value().toVariant());
                        statusBar()->showMessage(
                            QStringLiteral("已插入 %1 并预填 %2 个参数").arg(nodeType).arg(params.size()), 8000);
                        return true;
                    };
                    dlg.exec();
                });
        // 检测框标注（2026-10-28）：画框 → 同名 YOLO txt（cls cx cy w h 归一化），
        // Ultralytics / YOLOX 直接能吃，不需要再导出。
        connect(ui->menuSystem->addAction(QStringLiteral("检测框标注…")), &QAction::triggered,
                this, [this]() {
                    BoxAnnotatorDialog dlg(this);
                    dlg.exec();
                });
        // 训练（2026-10-29）：调用外部训练环境（YOLOX/Ultralytics）。
        // 环境不在本机时装不了就明说——产线机永远不需要它。
        connect(ui->menuSystem->addAction(QStringLiteral("训练…")), &QAction::triggered,
                this, [this]() {
                    TrainingRunnerDialog dlg(this);
                    dlg.exec();
                });
        // 打包模型与依赖（2026-10-28）：把 onnx/classes.txt 等外部文件收进方案目录，
        // 换机器拷方案就能跑——显式动作，不在保存时偷偷搬用户文件。
        // 让用户选方案目录（MainWindow 没有公开的"当前方案路径"，选目录比猜更明确）。
        m_packModelsAct = ui->menuFile->addAction(QStringLiteral("打包模型与依赖…"));
        connect(m_packModelsAct, &QAction::triggered, this, [this]() {
            // 入口再查一次（2026-10-29 r15 P2-13）：菜单可能在运行开始前就已打开
            auto *sess = SessionManager::instance();
            if (!sess->canEditScheme()
                || (m_executor && !m_executor->allowsGraphEditing())) {
                QMessageBox::warning(
                    this, QStringLiteral("暂时不能打包"),
                    QStringLiteral("流程正在运行，或写操作被禁止。\n"
                                   "停止后重试；出厂口令未改时请先改密。"));
                return;
            }
                    const QString dir = QFileDialog::getExistingDirectory(
                        this, QStringLiteral("选择方案目录（依赖将收进 <目录>/assets/）"));
                    if (dir.isEmpty())
                        return;
                    // 选目录用的是嵌套事件循环（2026-10-29 r16 P2-13）：这段时间里外部触发
                    // 仍可能把流程拉起来，返回后必须再查一次，否则会往正在跑的流程里改路径。
                    if (!SessionManager::instance()->canEditScheme()
                        || (m_executor && !m_executor->allowsGraphEditing())) {
                        QMessageBox::warning(
                            this, QStringLiteral("暂时不能打包"),
                            QStringLiteral("等待选择目录期间流程已开始运行。\n停止后再试。"));
                        return;
                    }
                    const QString scheme = dir + QStringLiteral("/scheme.vfp");
                    const ProjectPackager::Result r = ProjectPackager::pack(currentFlowScene(), scheme);
                    if (!r.ok) {
                        QMessageBox::warning(this, QStringLiteral("打包失败"), r.error);
                        return;
                    }
                    QString msg = r.copied.isEmpty()
                                      ? QStringLiteral("没有需要打包的外部依赖")
                                      : QStringLiteral("已打包 %1 个依赖到 assets/：\n%2\n\n"
                                                       "节点参数已改为相对路径，请保存方案以生效。")
                                            .arg(r.copied.size())
                                            .arg(r.copied.join(QStringLiteral("\n")));
                    QMessageBox::information(this, QStringLiteral("打包完成"), msg);
                });
        m_confirmScriptAct->setChecked(policy.requireConfirmation());
        m_menuRouter.direct(m_confirmScriptAct, this, [this]() {
            ScriptSecurityPolicy &p = ScriptSecurityPolicy::instance();
            const bool turningOff = !m_confirmScriptAct->isChecked();
            if (turningOff) {
                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(tr("关闭脚本执行前确认"));
                // 措辞按实际作用域写（2026-10-25 复核）：QSettings 默认构造按 **Windows 用户**
                // 隔离，所以只对"本 Windows 账户"下的会话生效，说"整机"是夸大。
                box.setText(tr("关闭后，方案里的脚本将以当前用户权限直接执行，不再弹窗确认。\n"
                               "该设置按 Windows 账户保存，对本账户下的全部登录会话生效。\n\n确定要关闭吗？"));
                QPushButton *yes = box.addButton(tr("仍要关闭"), QMessageBox::AcceptRole);
                QPushButton *no = box.addButton(tr("取消"), QMessageBox::RejectRole);
                box.setDefaultButton(no);   // 默认"取消"：回车不会误关
                box.exec();
                if (box.clickedButton() != yes) {
                    m_confirmScriptAct->setChecked(true);   // 回滚勾选，不改配置
                    return;
                }
            }
            p.setRequireConfirmation(!turningOff);
            p.save();   // 立即落 QSettings，下次启动保持一致
            const QString msg = turningOff
                ? tr("脚本执行前确认：已关闭——脚本将以当前用户权限直接执行")
                : tr("脚本执行前确认：已开启（同一脚本每次启动后首次执行前确认一次）");
            logMessage(msg);
            ui->statusBar->showMessage(msg, 8000);
            // 持久审计：谁把整机的脚本确认关掉了（重启后也能查）
            AppDatabase::instance()->logOperation(
                SessionManager::instance()->currentUser(),
                turningOff ? QStringLiteral("关闭脚本执行前确认") : QStringLiteral("开启脚本执行前确认"),
                msg);
        });
        if (!policy.requireConfirmation()) {
            // 启动留痕：关闭状态只有菜单勾选可见，现场极易忘记。
            // 这里只写日志——**状态栏提示放到登录成功之后**（2026-10-25 复核 P3）：
            // 本函数在构造期运行，登录框在其后才弹，此刻写的 15 秒提示会在窗口出现前就过期。
            logMessage(tr("注意：脚本执行前确认处于关闭状态（脚本将直接运行）——"
                          "可在「系统 → 脚本安全：执行前确认脚本」恢复"));
        }
    }

    m_menuRouter.direct(ui->actionCameraConfig, this,
                        [this]() { onManageGlobalCameras(); });

    m_menuRouter.toDialog(ui->actionUserManagement, this, QStringLiteral("userManagement"),
                          [this]() -> QDialog * { return new UserManagementDialog(this); });

    // 取当前流程的场景（与「流程变量」同一取法）：配方保存/加载都要靠它。
    // 必须把场景交进去——否则「新建配方」无从抓取参数（RecipeManager 收到 nullptr
    // 会立刻返回 false）、「加载配方」也无从写回，两处都会静默失败。
    // 缺场景的提示框留在窗侧（onMissing 回调；GUI 不进路由面）。
    m_menuRouter.toFlowSceneDialog(
        ui->actionRecipeManager, this, QStringLiteral("recipe"),
        [this]() { return currentFlowScene(); },
        [this]() {
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("请先打开一个流程，再使用配方管理。"));
        },
        [this](FlowScene *scene) -> QDialog * {
            auto *dlg = new RecipeDialog(this);
            dlg->setFlowScene(scene);
            return dlg;
        });

    m_menuRouter.toDialog(ui->actionAlarmHistory, this, QStringLiteral("alarmHistory"),
                          [this]() -> QDialog * { return new AlarmHistoryDialog(this); });

    m_menuRouter.toDialog(ui->actionInspectionResults, this, QStringLiteral("inspectionResults"),
                          [this]() -> QDialog * { return new InspectionResultDialog(this); });

    m_menuRouter.toScenesDialog(
        ui->actionParameterSearch, this, QStringLiteral("paramSearch"),
        [this]() { return m_flowScenes; },
        [this](const QList<FlowScene *> &scenes) -> QDialog * {
            return new ParameterSearchDialog(scenes, this);
        });

    m_menuRouter.toScenesDialog(
        ui->actionCodeExport, this, QStringLiteral("codeExport"),
        [this]() { return m_flowScenes; },
        [this](const QList<FlowScene *> &scenes) -> QDialog * {
            return new CodeExportDialog(scenes, this);
        });

    m_menuRouter.toDialog(ui->actionOperationLog, this, QStringLiteral("operationLog"),
                          [this]() -> QDialog * { return new OperationLogDialog(this); });

    m_menuRouter.toDialog(ui->actionReport, this, QStringLiteral("report"),
                          [this]() -> QDialog * { return new ReportDialog(this); });
}

void MainWindow::applyPermissionRestrictions()
{
    auto *session = SessionManager::instance();
    if (!session->isLoggedIn()) {
        // 未登录时对**新增的整机安全开关**显式 fail-closed（2026-10-25 复核 P3）：
        // 原实现直接 return，会保留上一次的 enabled 状态——当前唯一的 logout 路径发生在
        // 写闸开启时（菜单项已被禁用），所以不是现成绕过；但只要将来多一条"未开写闸也能
        // 登出"的路径，这项就会带着旧的可用态留在界面上。安全开关按最严处理。
        if (m_confirmScriptAct)
            m_confirmScriptAct->setEnabled(false);
        if (m_packModelsAct)
            m_packModelsAct->setEnabled(false);   // P2-13：改活方案路径，未登录一律禁用
        // 文件菜单的载入/写入类入口同样 fail-closed（未登录不得打开或覆盖本机方案）
        if (ui->actionOpenScheme)
            ui->actionOpenScheme->setEnabled(false);
        if (m_actionSaveAs)
            m_actionSaveAs->setEnabled(false);
        if (m_actionExportEncrypted)
            m_actionExportEncrypted->setEnabled(false);
        if (m_actionImportFlow)
            m_actionImportFlow->setEnabled(false);
        if (m_recentFiles && m_recentFiles->menu())
            m_recentFiles->menu()->setEnabled(false);
        if (ui->actionCodeExport)
            ui->actionCodeExport->setEnabled(false);
        if (m_editMenu)
            m_editMenu->setEnabled(false);
        if (m_actionAcquisitionStrategy)
            m_actionAcquisitionStrategy->setEnabled(false);
        return;
    }

    // 用户管理仅 Admin 可见/可用
    if (ui->actionUserManagement) {
        ui->actionUserManagement->setEnabled(session->canManageUsers());
    }
    // 方案编辑/保存/载入需 Engineer 以上：同一判据覆盖文件菜单全部写入类入口
    // （打开/最近打开/另存为/导出加密/导入流程），避免只灰了保存按钮而旁路仍可用。
    const bool canEdit = session->canEditScheme();
    if (ui->actionSave)
        ui->actionSave->setEnabled(canEdit);
    if (ui->actionLoad)
        ui->actionLoad->setEnabled(canEdit);
    if (ui->actionNew)
        ui->actionNew->setEnabled(canEdit);
    if (ui->actionOpenScheme)
        ui->actionOpenScheme->setEnabled(canEdit);
    if (m_actionSaveAs)
        m_actionSaveAs->setEnabled(canEdit);
    if (m_actionExportEncrypted)
        m_actionExportEncrypted->setEnabled(canEdit);
    if (m_actionImportFlow)
        m_actionImportFlow->setEnabled(canEdit);
    if (m_recentFiles && m_recentFiles->menu())
        m_recentFiles->menu()->setEnabled(canEdit);
    if (ui->actionCodeExport)
        ui->actionCodeExport->setEnabled(canEdit);
    if (m_editMenu)
        m_editMenu->setEnabled(canEdit);
    if (m_actionAcquisitionStrategy)
        m_actionAcquisitionStrategy->setEnabled(canEdit);
    // 脚本执行前确认是**整机**安全配置（关闭后影响本机所有会话），按 Engineer 及以上收口：
    // 操作员不能把产线的脚本护栏关掉（2026-10-25 复核 P2）。菜单项可能尚未建立（构造早期），
    // 空指针守卫兜住。
    if (m_confirmScriptAct) {
        m_confirmScriptAct->setEnabled(session->canEditScheme());
    }
    if (m_packModelsAct) {
        // 打包会改写活方案里的文件路径（2026-10-29 复核 P2-13）：写闸开着或画布锁着时不给用
        // 与画布锁同一判据（2026-10-29 r15 P2-13）：原先用 isRunning()，暂停等待里线程仍在跑
        // 但已允许改图 —— 两套布尔不一致。这里统一用 !allowsGraphEditing()。
        m_packModelsAct->setEnabled(
            session->canEditScheme()
            && !session->canvasEditShouldBeLocked(m_executor ? !m_executor->allowsGraphEditing()
                                                              : false));
    }
    if (ui->menuEdit) {
        ui->menuEdit->setEnabled(session->canEditScheme());
    }
    // W-1②：把 canConfigCommunications() 接进来。该谓词自 A1-① 起只有定义、没有任何生产
    // 调用点（A2/A7 点名的正是"写了不接等于没写"），而改密弹框一直写着"通信配置被禁止"。
    // 通信配置本身只随方案文件落盘（那一条 ProjectManager 已挡），但在闸开启期间仍能改
    // 活着的连接配置——这既没被声明过，也不是必要的，故与文案对齐：入口随闸灰掉。
    const bool canConfigComm = session->canConfigCommunications();
    if (ui->menuCommunication) {
        ui->menuCommunication->setEnabled(canConfigComm);
    }
    for (QAction *act : { ui->actionCommDevice, ui->actionCommReceiveEvent,
                          ui->actionCommSendEvent, ui->actionCommHeartbeat }) {
        if (act) {
            act->setEnabled(canConfigComm);
        }
    }
    // 边界（有意为之）：actionCommMonitor 是只读监视视图，不写任何配置 ⇒ 不因闸禁用
    if (ui->actionCommMonitor) {
        ui->actionCommMonitor->setEnabled(session->isLoggedIn());
    }
    // P0：闸/解除之后画布编辑锁随闸重算——运行态锁分量按各场景自己的执行器取，组合判据
    // 收在 SessionManager::canvasEditShouldBeLocked（与 716/720 的运行态接线同一枚决策件）。
    for (auto it = m_flowExecutors.constBegin(); it != m_flowExecutors.constEnd(); ++it) {
        if (FlowScene *scene = it.key()) {
            FlowExecutor *sceneEx = it.value() ? it.value()->executor() : nullptr;
            scene->setEditLocked(SessionManager::instance()->canvasEditShouldBeLocked(
                sceneEx ? !sceneEx->allowsGraphEditing() : false));
        }
    }
    // 角色变化后同步只读管控（只读态优先于角色权限禁用覆盖保存）
    applyReadonlyUI();
}

void MainWindow::enforceFactoryPasswordPolicy()
{
    auto *session = SessionManager::instance();
    const AdminPasswordState state = FactoryPasswordGuard::refreshWritesLock();
    if (state == AdminPasswordState::Retired) {
        applyPermissionRestrictions();
        return;
    }

    // 走到这里说明闸必须保持禁止：要么 admin/admin 仍然能用，要么这条判定今天做不出来。
    applyPermissionRestrictions();

    if (state == AdminPasswordState::Unreadable) {
        // 读不到用户表不等于出厂口令已改掉，所以闸不开。也不进下面的改密循环——那条路要求
        // 填写当前出厂口令，而此刻连「当前是什么」都判不出来；但原因必须当场给操作员看：
        // 一个静默的禁止会被当成程序故障绕过去，而绕过去的人正是这道闸要拦的那类。
        QMessageBox::warning(this, tr("无法判定出厂口令"),
                             tr("当前读不到用户表，无法确认管理员是否仍在使用出厂默认口令，"
                                "画布与模块编辑、保存/导出方案、用户增删与通信配置入口保持禁止。\n"
                                "请先确认数据库可读写，再走「系统 → 修改管理员口令」。\n\n%1")
                                 .arg(AppDatabase::instance()->lastDatabaseError()));
        ui->statusBar->showMessage(
            tr("写操作已被禁止：读不到用户表，无法确认出厂默认口令是否仍在用"), 0);
        logMessage(tr("出厂口令判定做不出来（用户表读不到）：写操作保持禁止"));
        return;
    }

    while (session->writesBlocked()) {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("修改管理员口令"));
        auto *info = new QLabel(tr("管理员账户 admin 仍在使用出厂默认口令，任何人都能免授权登录并修改方案。\n"
                                   "在改掉口令之前，画布与模块编辑、保存/导出方案、用户增删与通信配置入口均被禁止，"
                                   "新的流程运行也不受理"
                                   "（用户增删在落库层拒绝，不只是菜单置灰；正在跑的流程不受影响）。"),
                                &dlg);
        info->setWordWrap(true);
        auto *oldEdit = new QLineEdit(&dlg);
        auto *newEdit = new QLineEdit(&dlg);
        auto *confirmEdit = new QLineEdit(&dlg);
        for (QLineEdit *edit : { oldEdit, newEdit, confirmEdit }) {
            edit->setEchoMode(QLineEdit::Password);
        }
        auto *form = new QFormLayout();
        form->addRow(tr("当前出厂口令"), oldEdit);
        form->addRow(tr("新口令（至少 6 位）"), newEdit);
        form->addRow(tr("确认新口令"), confirmEdit);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        auto *layout = new QVBoxLayout(&dlg);
        layout->addWidget(info);
        layout->addLayout(form);
        layout->addWidget(buttons);

        if (dlg.exec() != QDialog::Accepted) {
            // 173 行拍板（P0 余量：取消改密后会话仍在）：放弃即退登——闸保持，会话不留。
            // 回程现成：登录判据不变（重登或重启后再进），改密弹框每次登录都会再弹直到改掉；
            // 「系统 → 修改管理员口令」刻意不受会话约束（setupSystemMenu 处 setEnabled(true)），
            // 退登后也能直接把闸解掉，不存在把用户锁死在无法解锁位置的问题。
            session->logout();
            setWindowTitle(QStringLiteral("视觉方案工作台"));
            applyPermissionRestrictions();
            logMessage(tr("放弃修改出厂口令：本次会话已结束，写闸保持禁止（重新登录后继续）"));
            break;
        }

        QString error;
        if (FactoryPasswordGuard::retireFactoryPassword(oldEdit->text(), newEdit->text(),
                                                       confirmEdit->text(), &error)) {
            applyPermissionRestrictions();
            ui->statusBar->clearMessage();
            QMessageBox::information(this, tr("修改管理员口令"), tr("出厂口令已停用，写操作已恢复。"));
            // 改密成功后写闸解除，之前**合并进写闸常驻提示**的"脚本确认已关"也跟着没了
            // （2026-10-25 复核：首次安装 + 当场改密这条窄分支）——补回同一条提示。
            // 必须放在信息框**之后**：模态框会冻结事件循环，写在前面的 15 秒提示会在用户读完
            // 信息框之前就过期（2026-10-25 复核 P3）。
            if (!ScriptSecurityPolicy::instance().requireConfirmation()) {
                ui->statusBar->showMessage(
                    tr("注意：脚本执行前确认处于关闭状态（脚本将直接运行）——"
                       "可在「系统 → 脚本安全：执行前确认脚本」恢复"), 15000);
            }
            break;
        }
        QMessageBox::warning(this, tr("修改管理员口令"), error);
        // 失败不退出循环：闸还在，继续要求改密
    }

    if (session->writesBlocked()) {
        // 常驻（timeout=0）：灰掉菜单不等于安全，真正的拒绝发生在 ProjectManager 落盘处，
        // 必须一直有个看得见的原因说明"为什么保存没反应"。
        QString gateMsg =
            tr("写操作已被禁止：出厂默认口令仍在使用（「系统 → 修改管理员口令」可解除）");
        // 首次安装时脚本确认的提示会被这条常驻提示顶掉（2026-10-25 复核）：两件都是"装完就该
        // 知道"的事，合并进同一条常驻提示，否则后写的盖掉前一条，现场只看得到一半。
        if (!ScriptSecurityPolicy::instance().requireConfirmation()) {
            gateMsg += tr("；且脚本执行前确认处于关闭状态（系统 → 脚本安全：执行前确认脚本）");
        }
        ui->statusBar->showMessage(gateMsg, 0);
        logMessage(tr("出厂默认口令仍在使用：写操作已禁止，直到管理员口令被修改"));
    }
}

void MainWindow::updateLanguage()
{
    // 更新界面语言
    VFP_DEBUG << "updateLanguage called";
    retranslateUi();
}

void MainWindow::retranslateUi()
{
    // 重新翻译UI（词条切片 B 就地链首抽：双语文本对全入 UITranslator，语言分派一跳）
    // 序偶性钉：条目序 = 原中文/英文两分支逐行序；设文回调内目标指针应用时才 deref（同瞬）
    VFP_DEBUG << "retranslateUi called";

    m_texts.clear();
    m_texts.add([this](const QString &s) { setWindowTitle(s); },
                QStringLiteral("视觉方案工作台"), QStringLiteral("Vision Inspection Workbench"));
    m_texts.add([this](const QString &s) { ui->menuFile->setTitle(s); },
                QStringLiteral("文件"), QStringLiteral("File"));
    m_texts.add([this](const QString &s) { ui->menuEdit->setTitle(s); },
                QStringLiteral("编辑"), QStringLiteral("Edit"));
    m_texts.add([this](const QString &s) { ui->menuView->setTitle(s); },
                QStringLiteral("视图"), QStringLiteral("View"));
    m_texts.add([this](const QString &s) { ui->menuCommunication->setTitle(s); },
                QStringLiteral("通讯"), QStringLiteral("Communication"));
    m_texts.add([this](const QString &s) { ui->menuSystem->setTitle(s); },
                QStringLiteral("系统"), QStringLiteral("System"));
    // 脚本确认开关（2026-10-25 复核 P3：新菜单项此前硬编码中文，英文界面下漏翻）
    m_texts.add([this](const QString &s) {
        if (m_confirmScriptAct)
            m_confirmScriptAct->setText(s);
    }, QStringLiteral("脚本安全：执行前确认脚本（取消勾选则直接执行）"),
       QStringLiteral("Script safety: confirm scripts before running (uncheck = run directly)"));

    m_texts.add([this](const QString &s) { ui->actionNew->setText(s); },
                QStringLiteral("新建方案"), QStringLiteral("New Scheme"));
    m_texts.add([this](const QString &s) { ui->actionSave->setText(s); },
                QStringLiteral("保存方案"), QStringLiteral("Save Scheme"));
    m_texts.add([this](const QString &s) { ui->actionLoad->setText(s); },
                QStringLiteral("打开方案"), QStringLiteral("Open Scheme"));
    m_texts.add([this](const QString &s) { ui->actionExit->setText(s); },
                QStringLiteral("退出"), QStringLiteral("Exit"));

    m_texts.add([this](const QString &s) { ui->actionAddFlow->setText(s); },
                QStringLiteral("添加流程"), QStringLiteral("Add Flow"));
    m_texts.add([this](const QString &s) { ui->actionDeleteFlow->setText(s); },
                QStringLiteral("删除流程"), QStringLiteral("Delete Flow"));
    m_texts.add([this](const QString &s) { if (m_actionCreateGroup) m_actionCreateGroup->setText(s); },
                QStringLiteral("创建分组"), QStringLiteral("Create Group"));
    m_texts.add([this](const QString &s) { if (m_actionDissolveGroup) m_actionDissolveGroup->setText(s); },
                QStringLiteral("解散分组"), QStringLiteral("Dissolve Group"));
    m_texts.add([this](const QString &s) { if (m_actionToggleGroup) m_actionToggleGroup->setText(s); },
                QStringLiteral("折叠/展开分组"), QStringLiteral("Collapse/Expand Group"));
    m_texts.add([this](const QString &s) { if (m_actionCopySnippet) m_actionCopySnippet->setText(s); },
                QStringLiteral("复制所选算子"), QStringLiteral("Copy Selected Nodes"));
    m_texts.add([this](const QString &s) { if (m_actionPasteSnippet) m_actionPasteSnippet->setText(s); },
                QStringLiteral("粘贴"), QStringLiteral("Paste"));
    m_texts.add([this](const QString &s) { if (m_actionExportSnippet) m_actionExportSnippet->setText(s); },
                QStringLiteral("导出片段…"), QStringLiteral("Export Snippet…"));
    m_texts.add([this](const QString &s) { if (m_actionImportSnippet) m_actionImportSnippet->setText(s); },
                QStringLiteral("导入片段…"), QStringLiteral("Import Snippet…"));

    m_texts.add([this](const QString &s) { ui->actionSaveScheme->setText(s); },
                QStringLiteral("保存方案"), QStringLiteral("Save Scheme"));
    m_texts.add([this](const QString &s) { ui->actionOpenScheme->setText(s); },
                QStringLiteral("打开方案"), QStringLiteral("Open Scheme"));

    m_texts.add([this](const QString &s) { ui->actionStartExecution->setText(s); },
                QStringLiteral("开始执行"), QStringLiteral("Start"));
    m_texts.add([this](const QString &s) { ui->actionStopExecution->setText(s); },
                QStringLiteral("停止执行"), QStringLiteral("Stop"));
    m_texts.add([this](const QString &s) { ui->actionRuntimeDesigner->setText(s); },
                QStringLiteral("运行界面设计"), QStringLiteral("Runtime UI Designer"));
    m_texts.add([this](const QString &s) { if (m_runtimeViewDock) m_runtimeViewDock->setWindowTitle(s); },
                QStringLiteral("运行界面"), QStringLiteral("Runtime UI"));

    m_texts.add([this](const QString &s) { ui->imageDock->setWindowTitle(s); },
                QStringLiteral("图像显示"), QStringLiteral("Image Display"));
    m_texts.add([this](const QString &s) { ui->paramDock->setWindowTitle(s); },
                QStringLiteral("算法参数"), QStringLiteral("Tool Parameters"));
    // 实装 4.4 口径：左侧停靠面板叫“工具箱”（zh-cn.xaml: 工具箱 / Tool Box）
    m_texts.add([this](const QString &s) { ui->toolDock->setWindowTitle(s); },
                QStringLiteral("工具箱"), QStringLiteral("Toolbox"));
    m_texts.add([this](const QString &s) { ui->actionToggleToolDock->setText(s); },
                QStringLiteral("显示/隐藏工具箱"), QStringLiteral("Show/Hide Toolbox"));
    m_texts.add([this](const QString &s) { ui->actionParameterSearch->setText(s); },
                QStringLiteral("参数查找工具"), QStringLiteral("Parameter Search Tool"));

    m_texts.apply(m_currentLanguage == Language::Chinese ? UITranslator::Lang::Zh
                                                         : UITranslator::Lang::En);

    // 更新参数面板标签页名称（原两分支末位同形，落表外同位）
    if (m_selectedNode) {
        showNodeParameters(m_selectedNode);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    static QTreeWidgetItem *draggedItem = nullptr;
    static QPoint dragStartPos;
    
    try {
        // 检查obj是否有效
        if (!obj) {
            return QMainWindow::eventFilter(obj, event);
        }
        
        // 检查ui和toolLibrary是否有效
        if (!ui || !ui->toolLibrary) {
            return QMainWindow::eventFilter(obj, event);
        }
        
        if (obj == ui->toolLibrary->viewport()) {
            if (event->type() == QEvent::MouseButtonPress) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent && mouseEvent->button() == Qt::LeftButton) {
                    QTreeWidgetItem *item = ui->toolLibrary->itemAt(mouseEvent->pos());
                    if (item && !item->data(0, Qt::UserRole).toString().isEmpty()) {
                        draggedItem = item;
                        dragStartPos = mouseEvent->pos();
                    }
                }
            } else if (event->type() == QEvent::MouseMove) {
                QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent && draggedItem && (mouseEvent->pos() - dragStartPos).manhattanLength() > QApplication::startDragDistance()) {
                    // 创建拖拽对象
                    QDrag *drag = new QDrag(ui->toolLibrary);
                    QMimeData *mimeData = new QMimeData;
                    
                    // 设置mimeData文本为节点名称
                    mimeData->setText(draggedItem->text(0));
                    const QString paletteId = draggedItem->data(0, Qt::UserRole).toString().trimmed();
                    if (!paletteId.isEmpty()) {
                        mimeData->setData(NodeFactory::toolPaletteMimeFormat(),
                                          paletteId.toUtf8());
                    }
                    drag->setMimeData(mimeData);
                    
                    // 执行拖拽（非阻塞）
                    drag->exec(Qt::CopyAction, Qt::CopyAction);
                    draggedItem = nullptr;
                }
            } else if (event->type() == QEvent::MouseButtonRelease) {
                draggedItem = nullptr;
            }
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in eventFilter:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in eventFilter";
    }
    
    return QMainWindow::eventFilter(obj, event);
}

/// 转发：显示决策（下拉框选择 > 画布选中节点 > 兜底）在 ImageDisplayController
NodeBase *MainWindow::resolveDisplayNode(NodeBase *fallbackNode) const
{
    return m_imageDisplay->resolveDisplayNode(fallbackNode);
}

FlowCanvasView *MainWindow::canvasViewForScene(FlowScene *scene) const
{
    if (!scene)
        return nullptr;
    for (QGraphicsView *view : scene->views()) {
        if (auto *canvas = qobject_cast<FlowCanvasView *>(view))
            return canvas;
    }
    return nullptr;
}

void MainWindow::updateZoomStatus()
{
    if (!m_zoomStatusLabel)
        return;
    // 无流程（启动早期/全部关闭）时按 100% 显示，避免状态栏出现空白项
    FlowCanvasView *view = canvasViewForScene(currentFlowScene());
    const int percent = view ? view->zoomPercent() : 100;
    m_zoomStatusLabel->setText(tr("缩放 %1%").arg(percent));
}

void MainWindow::updateStatusHint()
{
    if (!m_statusHintLabel)
        return;
    FlowScene *scene = currentFlowScene();
    const int nodeCount = scene ? scene->nodes().size() : 0;
    // VM 默认空流程提示；有模块后收敛为「就绪」（操作提示仍可由 showMessage 临时覆盖）
    m_statusHintLabel->setText(nodeCount == 0 ? tr("等待添加模块") : tr("就绪"));
}

void MainWindow::showCrosshairSettings()
{
    if (!m_imageView)
        return;

    const QImage image = m_imageView->currentQImage();
    const bool hasImage = !image.isNull();
    const QVector<HalconWindow::CrosshairConfig> current = m_imageView->crosshairConfigs();

    struct RowWidgets {
        QCheckBox *enabled = nullptr;
        QDoubleSpinBox *x = nullptr;
        QDoubleSpinBox *y = nullptr;
        QSpinBox *thickness = nullptr;
        QCheckBox *locked = nullptr;
        QPushButton *colorBtn = nullptr;
        QPushButton *centerBtn = nullptr;
        std::shared_ptr<QColor> color;
    };
    QVector<RowWidgets> rows;

    auto styleColorButton = [](QPushButton *btn, const QColor &color) {
        btn->setStyleSheet(QStringLiteral("QPushButton { background:%1; color:%2; }")
                               .arg(color.name(),
                                    color.lightness() > 128 ? QStringLiteral("#000")
                                                            : QStringLiteral("#fff")));
        btn->setText(color.name());
    };

    QDialog dialog(this);
    dialog.setWindowTitle(tr("十字辅助线配置"));
    auto *root = new QVBoxLayout(&dialog);

    for (int i = 0; i < 2; ++i) {
        const HalconWindow::CrosshairConfig cfg =
            (i < current.size()) ? current.at(i) : HalconWindow::CrosshairConfig();
        auto *box = new QGroupBox(tr("十字辅助线%1").arg(i + 1), &dialog);
        auto *form = new QFormLayout(box);

        RowWidgets row;
        row.color = std::make_shared<QColor>(cfg.color);
        row.enabled = new QCheckBox(tr("启用"), box);
        row.enabled->setChecked(cfg.visible);
        form->addRow(row.enabled);

        const double maxX = hasImage ? qMax(0, image.width() - 1) : 9999.0;
        const double maxY = hasImage ? qMax(0, image.height() - 1) : 9999.0;

        row.x = new QDoubleSpinBox(box);
        row.x->setDecimals(1);
        row.x->setRange(0.0, maxX);
        row.x->setSuffix(QStringLiteral(" px"));
        row.x->setValue(hasImage ? qBound(0.0, cfg.x < 0.0 ? image.width() / 2.0 : cfg.x, maxX) : 0.0);
        row.x->setEnabled(hasImage);
        form->addRow(tr("坐标X"), row.x);

        row.y = new QDoubleSpinBox(box);
        row.y->setDecimals(1);
        row.y->setRange(0.0, maxY);
        row.y->setSuffix(QStringLiteral(" px"));
        row.y->setValue(hasImage ? qBound(0.0, cfg.y < 0.0 ? image.height() / 2.0 : cfg.y, maxY) : 0.0);
        row.y->setEnabled(hasImage);
        form->addRow(tr("坐标Y"), row.y);

        row.thickness = new QSpinBox(box);
        row.thickness->setRange(1, 10);
        row.thickness->setValue(qBound(1, cfg.thickness, 10));
        form->addRow(tr("粗细"), row.thickness);

        row.colorBtn = new QPushButton(box);
        styleColorButton(row.colorBtn, *row.color);
        connect(row.colorBtn, &QPushButton::clicked, &dialog,
                [this, &dialog, btn = row.colorBtn, color = row.color, styleColorButton]() {
                    const QColor chosen =
                        QColorDialog::getColor(*color, &dialog, tr("十字线颜色"));
                    if (!chosen.isValid())
                        return;
                    *color = chosen;
                    styleColorButton(btn, chosen);
                });
        form->addRow(tr("颜色"), row.colorBtn);

        row.centerBtn = new QPushButton(tr("居中"), box);
        row.centerBtn->setEnabled(hasImage);
        connect(row.centerBtn, &QPushButton::clicked, &dialog,
                [xSpin = row.x, ySpin = row.y, image]() {
                    xSpin->setValue(image.width() / 2.0);
                    ySpin->setValue(image.height() / 2.0);
                });
        form->addRow(row.centerBtn);

        row.locked = new QCheckBox(tr("锁定（禁止在预览画面上拖动）"), box);
        row.locked->setChecked(cfg.locked);
        form->addRow(row.locked);

        rows.append(row);
        root->addWidget(box);
    }

    if (!hasImage) {
        auto *note = new QLabel(tr("当前没有图像：坐标将在首帧显示时自动居中，之后可直接拖动辅助线。"),
                                &dialog);
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color:#8f9aa8;"));
        root->addWidget(note);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QVector<HalconWindow::CrosshairConfig> configs(2);
    QSettings settings;
    int visibleCount = 0;
    for (int i = 0; i < 2; ++i) {
        const RowWidgets &row = rows.at(i);
        HalconWindow::CrosshairConfig &cfg = configs[i];
        cfg.visible = row.enabled->isChecked();
        cfg.locked = row.locked->isChecked();
        cfg.thickness = row.thickness->value();
        cfg.color = *row.color;
        if (hasImage) {
            cfg.x = row.x->value();
            cfg.y = row.y->value();
        } else {
            cfg.x = (i < current.size()) ? current.at(i).x : -1.0;
            cfg.y = (i < current.size()) ? current.at(i).y : -1.0;
        }
        if (cfg.visible)
            ++visibleCount;

        const QString prefix = QStringLiteral("display/crosshair%1/").arg(i + 1);
        settings.setValue(prefix + QStringLiteral("enabled"), cfg.visible);
        settings.setValue(prefix + QStringLiteral("x"), cfg.x);
        settings.setValue(prefix + QStringLiteral("y"), cfg.y);
        settings.setValue(prefix + QStringLiteral("thickness"), cfg.thickness);
        settings.setValue(prefix + QStringLiteral("color"), cfg.color.name());
        settings.setValue(prefix + QStringLiteral("locked"), cfg.locked);
    }

    m_imageView->setCrosshairConfigs(configs);
    if (m_imageView2)
        m_imageView2->setCrosshairConfigs(configs);
    if (auto *crossButton = findChild<QPushButton *>(QStringLiteral("imgCrossBtn")))
        crossButton->setChecked(visibleCount > 0);

    logMessage(tr("十字辅助线已更新：启用 %1 条").arg(visibleCount));
}

void MainWindow::showFullScreenImage()
{
    if (!m_imageView)
        return;
    const QImage image = m_imageView->currentQImage();
    if (image.isNull()) {
        QMessageBox::information(this, tr("全屏"), tr("当前没有可显示的图像"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("图像全屏（Esc 退出）"));
    dialog.setStyleSheet(QStringLiteral("background:#000000;"));
    dialog.setWindowState(Qt::WindowFullScreen);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *view = new HalconWindow(&dialog);
    view->setQImage(image, tr("全屏 - %1×%2").arg(image.width()).arg(image.height()));
    view->setOverlay(m_imageView->overlayShapes());
    view->setCrosshairConfigs(m_imageView->crosshairConfigs());
    layout->addWidget(view);
    dialog.setModal(true);
    dialog.exec();
}

QList<NodeBase *> MainWindow::upstreamNodes(NodeBase *node) const
{
    QList<NodeBase *> result;
    if (!node)
        return result;
    QSet<NodeBase *> visited;
    visited.insert(node);
    QList<NodeBase *> pending{node};
    while (!pending.isEmpty()) {
        NodeBase *current = pending.takeFirst();
        if (!current)
            continue;
        for (Port *port : current->inputPorts()) {
            if (!port)
                continue;
            for (MyConnection *connection : port->connections()) {
                if (!connection || !connection->sourcePort())
                    continue;
                NodeBase *source = connection->sourcePort()->node();
                if (source && !visited.contains(source)) {
                    visited.insert(source);
                    result.append(source);
                    pending.append(source);
                }
            }
        }
    }
    return result;
}

void MainWindow::applyExtraOverlays(NodeBase *displayNode)
{
    Q_UNUSED(displayNode)
    if (!m_imageView)
        return;
    // 实装 4.4：叠加的是“图像”而非 ROI/测量图元——把勾选模块的输出图以 50% 透明叠画
    QVector<HalconCpp::HImage> images;
    FlowScene *scene = currentFlowScene();
    for (NodeBase *node : m_overlayNodes) {
        if (!node || !scene || !scene->nodes().contains(node))
            continue;
        auto data = node->getOutputData(0);
        if (!data)
            continue;
        const HalconCpp::HImage image = data->getHImage();
        if (image.IsInitialized())
            images.append(image);
    }
    m_imageView->setHImageOverlays(images);
    if (m_imageView2)
        m_imageView2->setHImageOverlays(images);
}

void MainWindow::refreshLocalImagePanel(NodeBase *node)
{
    if (!m_localImageGroup || !m_localImageList)
        return;

    m_localImageNode = qobject_cast<ImageReadNode *>(node);
    m_localImageList->clear();
    if (!m_localImageNode) {
        m_localImageGroup->setVisible(false);
        return;
    }

    const QStringList files = m_localImageNode->localImageFiles();
    for (const QString &file : files)
        m_localImageList->addItem(QFileInfo(file).fileName());
    const int current = m_localImageNode->currentLocalImageIndex();
    if (current >= 0 && current < m_localImageList->count()) {
        m_localImageList->setCurrentRow(current);
        m_localImageList->scrollToItem(m_localImageList->item(current));
    }

    if (m_localIndexLabel) {
        m_localIndexLabel->setText(QStringLiteral("%1 / %2")
                                       .arg(current >= 0 ? current + 1 : 0)
                                       .arg(files.size()));
    }
    const bool hasImages = !files.isEmpty();
    if (m_localPrevBtn)
        m_localPrevBtn->setEnabled(hasImages);
    if (m_localNextBtn)
        m_localNextBtn->setEnabled(hasImages);
    if (m_localAutoSwitch) {
        QSignalBlocker blocker(m_localAutoSwitch);   // 回显不反写参数
        m_localAutoSwitch->setChecked(
            m_localImageNode->getParam(QStringLiteral("autoSwitch")).toBool());
    }

    m_localImageGroup->setTitle(
        tr("本地图 - %1（%2 张）").arg(m_localImageNode->name()).arg(files.size()));
    m_localImageGroup->setVisible(true);
}

void MainWindow::updateCanvasSelectionElapsed(NodeBase *node)
{
    FlowScene *scene = currentFlowScene();
    if (node && scene && !scene->nodes().contains(node)) {
        for (FlowScene *candidate : m_flowScenes) {
            if (candidate && candidate->nodes().contains(node)) {
                scene = candidate;
                break;
            }
        }
    }
    FlowCanvasView *view = canvasViewForScene(scene);
    if (!view)
        return;
    if (!node) {
        view->clearSelectedNodeElapsed();
        return;
    }
    const qint64 elapsed = m_nodeElapsed.value(node, -1);
    if (elapsed >= 0)
        view->setSelectedNodeElapsed(node->name(), elapsed);
    else
        view->clearSelectedNodeElapsed();
}

void MainWindow::onOpenNodeSearch()
{
    // 打开节点搜索对话框
    if (!m_nodeSearchWidget) {
        m_nodeSearchWidget = new NodeSearchWidget(this);
    }

    // 设置当前流程场景
    int currentIndex = ui->flowTabs->currentIndex();
    if (currentIndex >= 0 && currentIndex < m_flowScenes.size()) {
        m_nodeSearchWidget->setFlowScene(m_flowScenes[currentIndex]);
    }

    m_nodeSearchWidget->showAndFocus();
}

void MainWindow::onOpenPerformancePanel()
{
    // 打开性能面板（创建/显示在 AuxPanelManager）
    m_auxPanels->open(QStringLiteral("performance"));

    // 面板必须显式 bindExecutor() 才会收到耗时数据；此前无人调用，即使打开也是空表
    // （这是它长期不可达之外的**第二层**缺口）。
    if (auto *pp = m_auxPanels->performancePanel()) {
        if (m_executor)
            pp->bindExecutor(m_executor);
    }
}

void MainWindow::onOpenResultTable()
{
    // 打开结果数据表：历史结果 / 帮助（当前轮结果常驻在图像区「模块结果」页）
    m_auxPanels->open(QStringLiteral("resultTable"));
    logMessage(QStringLiteral("结果表已打开：查看历史结果与帮助；当前轮结果见右侧「模块结果」页"));
}

void MainWindow::onOpenOutputViewer()
{
    // 打开输出数据查看器
    m_auxPanels->open(QStringLiteral("outputData"));

    // 如果有选中的节点，显示其输出数据
    if (m_selectedNode) {
        if (auto *ov = m_auxPanels->outputDataViewer())
            ov->setNode(m_selectedNode);
    }
}

void MainWindow::applyDefaultDockSizes()
{
    if (!ui)
        return;

    const int winH = qMax(height(), 720);
    const int winW = qMax(width(), 1100);
    // 右侧（图像显示 / 参数配置 标签页）默认占窗口宽度约 50%：
    // 实测 VM 4.4 默认分栏在 1366 宽下图像区左边界 ≈x=680（≈50%），流程区与图像区基本对半；
    // 旧值 35% 是"工具箱展开"时的估算，与 VM 默认（工具箱收起为图标竖条）不符。
    // 图像区工具行已改为 QToolBar（窄了自动进溢出菜单），不会再把流程画布挤成一条缝。
    const int rightW = qBound(340, static_cast<int>(winW * 0.50), 1000);
    const int imageH = qBound(320, static_cast<int>(winH * 0.58), winH - 220);
    const int paramH = qBound(160, winH - imageH - 80, 360);

    QList<QDockWidget *> rightDocks;
    QList<int> rightSizes;
    if (ui->imageDock && ui->imageDock->isVisible() && !ui->imageDock->isFloating()) {
        rightDocks.append(ui->imageDock);
        rightSizes.append(imageH);
    }
    if (ui->paramDock && ui->paramDock->isVisible()) {
        rightDocks.append(ui->paramDock);
        rightSizes.append(paramH);
    }
    // 选项卡式停靠（FR4.3）时两个子页面共享同一区域，高度由区域统一决定，无需分别设置
    const bool tabbedSubPages = ui->imageDock && !tabifiedDockWidgets(ui->imageDock).isEmpty();
    if (rightDocks.size() >= 2 && !tabbedSubPages)
        resizeDocks(rightDocks, rightSizes, Qt::Vertical);
    if (ui->imageDock && !ui->imageDock->isFloating())
        resizeDocks({ui->imageDock}, {rightW}, Qt::Horizontal);
    if (ui->toolDock)
        resizeDocks({ui->toolDock}, {m_toolBrowserExpanded ? 200 : 48}, Qt::Horizontal);
    if (ui->logDock)
        resizeDocks({ui->logDock}, {120}, Qt::Vertical);
    // 底部结果区（图像区下方常驻）：约窗口 1/4 高，夹在 140-240
    if (QDockWidget *resultDock = m_auxPanels ? m_auxPanels->resultTableDock() : nullptr) {
        if (!resultDock->isFloating())
            resizeDocks({resultDock}, {qBound(140, winH / 4, 240)}, Qt::Vertical);
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    updateStatusHint();   // 启动/再显示时按当前流程模块数刷新左侧提示
    static bool s_firstShow = true;
    if (s_firstShow) {
        s_firstShow = false;
        QTimer::singleShot(0, this, [this]() {
            // 用户上次退出时拖过的停靠比例优先，只有没有存档时才套默认比例
            if (!m_dockLayoutRestored) {
                applyDefaultDockSizes();
            } else {
                // 存档可能是"面板还在但被拖成 0 宽"的坏布局：启动时给一个可用宽度，
                // 否则用户看到的是只有流程画布、工具箱/图像显示都不见了。
                if (ui->toolDock && !ui->toolDock->isFloating()) {
                    if (!m_toolBrowserExpanded)
                        resizeDocks({ui->toolDock}, {48}, Qt::Horizontal);   // 收起态 = 图标竖条
                    else if (ui->toolDock->width() < 140)
                        resizeDocks({ui->toolDock}, {220}, Qt::Horizontal);
                }
                if (ui->imageDock && !ui->imageDock->isFloating() && ui->imageDock->width() < 220)
                    resizeDocks({ui->imageDock}, {qMax(360, width() / 3)}, Qt::Horizontal);
                // 结果区常驻：旧存档可能把它拖成 0 高（含"关闭后显隐记录"），给一个可读高度
                if (QDockWidget *resultDock = m_auxPanels ? m_auxPanels->resultTableDock() : nullptr) {
                    if (!resultDock->isFloating() && resultDock->height() < 110)
                        resizeDocks({resultDock}, {160}, Qt::Vertical);
                }
            }
            // 崩溃恢复询问放在界面搭好之后：模态框不在 showEvent 里直接弹，避免挡住首帧
            checkRecoveryOnStartup();
        });
    }
}

void MainWindow::onToggleImageFloat()
{
    if (!ui || !ui->imageDock)
        return;
    if (ui->imageDock->isFloating()) {
        ui->imageDock->setFloating(false);
        addDockWidget(Qt::RightDockWidgetArea, ui->imageDock);
        if (ui->paramDock) {
            // 还原为「两个可切换子页面」的默认形态（FR4.3）：选项卡式停靠
            tabifyDockWidget(ui->imageDock, ui->paramDock);
            ui->imageDock->raise();
        }
        ui->imageDock->show();
        applyDefaultDockSizes();
    } else {
        ui->imageDock->setFloating(true);
        ui->imageDock->show();
        ui->imageDock->raise();
        ui->imageDock->resize(qMax(900, width() / 2), qMax(700, height() - 80));
        if (m_imageView)
            m_imageView->fitToWindow();
    }
}
