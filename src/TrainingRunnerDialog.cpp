#include "TrainingRunnerDialog.h"

#include <QtConcurrent>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

TrainingRunnerDialog::TrainingRunnerDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("训练（外部训练环境）"));
    resize(980, 700);

    auto *root = new QVBoxLayout(this);

    // ── 环境 ──
    auto *envRow = new QHBoxLayout();
    envRow->addWidget(new QLabel(QStringLiteral("Python：")));
    m_python = new QLineEdit();
    m_python->setPlaceholderText(QStringLiteral("例如 C:\\YOLOX\\.venv\\Scripts\\python.exe"));
    envRow->addWidget(m_python, 1);
    auto *browsePy = new QPushButton(QStringLiteral("浏览…"));
    connect(browsePy, &QPushButton::clicked, this, &TrainingRunnerDialog::onBrowsePython);
    envRow->addWidget(browsePy);
    m_probeBtn = new QPushButton(QStringLiteral("检测环境"));
    connect(m_probeBtn, &QPushButton::clicked, this, &TrainingRunnerDialog::onProbeEnv);
    envRow->addWidget(m_probeBtn);
    root->addLayout(envRow);

    m_envLabel = new QLabel(QStringLiteral("未检测：点「检测环境」确认训练环境是否就绪"));
    m_envLabel->setWordWrap(true);
    root->addWidget(m_envLabel);

    // ── 训练参数 ──
    auto *form = new QFormLayout();
    m_trainer = new QComboBox();
    m_trainer->addItems({QStringLiteral("YOLOX（Apache-2.0，可商用闭源）"),
                         QStringLiteral("Ultralytics YOLOv8/v11（AGPL）")});
    connect(m_trainer, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TrainingRunnerDialog::refreshCommand);
    form->addRow(QStringLiteral("训练器"), m_trainer);

    auto *yoloxRow = new QHBoxLayout();
    m_yoloxDir = new QLineEdit();
    m_yoloxDir->setPlaceholderText(QStringLiteral("YOLOX 源码目录（训练时作为工作目录）"));
    yoloxRow->addWidget(m_yoloxDir, 1);
    auto *browseY = new QPushButton(QStringLiteral("浏览…"));
    connect(browseY, &QPushButton::clicked, this, &TrainingRunnerDialog::onBrowseYolox);
    yoloxRow->addWidget(browseY);
    form->addRow(QStringLiteral("YOLOX 目录"), yoloxRow);

    auto *expRow = new QHBoxLayout();
    m_expOrWeights = new QLineEdit();
    m_expOrWeights->setPlaceholderText(QStringLiteral("YOLOX: exps/default/yolox_s.py；Ultralytics: yolov8n.pt"));
    expRow->addWidget(m_expOrWeights, 1);
    auto *browseE = new QPushButton(QStringLiteral("浏览…"));
    connect(browseE, &QPushButton::clicked, this, &TrainingRunnerDialog::onBrowseExp);
    expRow->addWidget(browseE);
    form->addRow(QStringLiteral("实验/权重"), expRow);

    m_dataYaml = new QLineEdit();
    m_dataYaml->setPlaceholderText(QStringLiteral("仅 Ultralytics 用：data.yaml（YOLOX 的数据集写在 exp 里）"));
    form->addRow(QStringLiteral("数据集 yaml"), m_dataYaml);

    auto *numRow = new QHBoxLayout();
    m_epochs = new QSpinBox();
    m_epochs->setRange(1, 10000);
    m_epochs->setValue(100);
    m_epochs->setToolTip(QStringLiteral("仅 Ultralytics 生效；YOLOX 的轮数在 exp 文件里改"));
    numRow->addWidget(new QLabel(QStringLiteral("epochs")));
    numRow->addWidget(m_epochs);
    m_imgsz = new QSpinBox();
    m_imgsz->setRange(64, 2048);
    m_imgsz->setValue(640);
    numRow->addWidget(new QLabel(QStringLiteral("imgsz")));
    numRow->addWidget(m_imgsz);
    m_batch = new QSpinBox();
    m_batch->setRange(1, 512);
    m_batch->setValue(8);
    numRow->addWidget(new QLabel(QStringLiteral("batch")));
    numRow->addWidget(m_batch);
    m_device = new QComboBox();
    m_device->addItems({QStringLiteral("CPU"), QStringLiteral("GPU 0"), QStringLiteral("GPU 1")});
    numRow->addWidget(new QLabel(QStringLiteral("device")));
    numRow->addWidget(m_device);
    form->addRow(QStringLiteral("超参"), numRow);

    auto *ckptRow = new QHBoxLayout();
    m_ckpt = new QLineEdit();
    m_ckpt->setPlaceholderText(QStringLiteral("可选：从已有权重续训（YOLOX -c）"));
    ckptRow->addWidget(m_ckpt, 1);
    auto *browseC = new QPushButton(QStringLiteral("浏览…"));
    connect(browseC, &QPushButton::clicked, this, &TrainingRunnerDialog::onBrowseCkpt);
    ckptRow->addWidget(browseC);
    form->addRow(QStringLiteral("续训权重"), ckptRow);

    m_extra = new QLineEdit();
    m_extra->setPlaceholderText(QStringLiteral("额外参数（原样拼在命令末尾），如 --fp16 -o"));
    form->addRow(QStringLiteral("额外参数"), m_extra);
    root->addLayout(form);

    // ── 命令预览 ──
    root->addWidget(new QLabel(QStringLiteral("将要执行的命令（只读）：")));
    m_commandPreview = new QPlainTextEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setMaximumHeight(70);
    root->addWidget(m_commandPreview);

    // ── 导出 ONNX 命令（只生成不执行：环境是否有 onnx 由训练机决定） ──
    auto *exportHead = new QHBoxLayout();
    exportHead->addWidget(new QLabel(QStringLiteral("导出 ONNX 命令（在训练机上执行）：")));
    exportHead->addStretch();
    auto *copyExportBtn = new QPushButton(QStringLiteral("复制导出命令"));
    connect(copyExportBtn, &QPushButton::clicked, this, &TrainingRunnerDialog::onCopyExportCommand);
    exportHead->addWidget(copyExportBtn);
    root->addLayout(exportHead);
    m_exportPreview = new QPlainTextEdit();
    m_exportPreview->setReadOnly(true);
    m_exportPreview->setMaximumHeight(56);
    root->addWidget(m_exportPreview);

    // ── 训练产物 → 回填节点（闭环的最后一环） ──
    auto *artifactHead = new QHBoxLayout();
    artifactHead->addWidget(new QLabel(QStringLiteral("训练产物：")));
    m_artifactLabel = new QLabel(QStringLiteral("尚未发现 ONNX 模型"));
    m_artifactLabel->setWordWrap(true);
    artifactHead->addWidget(m_artifactLabel, 1);
    auto *chooseOnnx = new QPushButton(QStringLiteral("选择 ONNX…"));
    connect(chooseOnnx, &QPushButton::clicked, this, &TrainingRunnerDialog::onChooseOnnx);
    artifactHead->addWidget(chooseOnnx);
    root->addLayout(artifactHead);
    auto *applyRow = new QHBoxLayout();
    m_detectBtn = new QPushButton(QStringLiteral("插入 ONNX目标检测"));
    m_countBtn = new QPushButton(QStringLiteral("插入 流水线计数"));
    connect(m_detectBtn, &QPushButton::clicked, this, &TrainingRunnerDialog::onApplyDetect);
    connect(m_countBtn, &QPushButton::clicked, this, &TrainingRunnerDialog::onApplyCounting);
    applyRow->addWidget(m_detectBtn);
    applyRow->addWidget(m_countBtn);
    applyRow->addStretch();
    root->addLayout(applyRow);

    // ── 按钮 ──
    auto *btns = new QHBoxLayout();
    m_startBtn = new QPushButton(QStringLiteral("开始训练"));
    connect(m_startBtn, &QPushButton::clicked, this, &TrainingRunnerDialog::onStart);
    m_cancelBtn = new QPushButton(QStringLiteral("取消训练"));
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TrainingRunnerDialog::onCancel);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"));
    // 走 close()（经 closeEvent 守卫），不要连 QDialog::accept（2026-10-29 P2-3）：
    // accept() 不经过 closeEvent/reject，训练中点「关闭」会直接硬杀、没有收尾机会。
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    btns->addWidget(m_startBtn);
    btns->addWidget(m_cancelBtn);
    btns->addStretch();
    btns->addWidget(closeBtn);
    root->addLayout(btns);

    // ── 日志 ──
    m_log = new QPlainTextEdit();
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(QStringLiteral("训练日志（实时）"));
    root->addWidget(m_log, 1);

    m_runner = new TrainingRunner(this);
    connect(m_runner, &TrainingRunner::logLine, this, &TrainingRunnerDialog::appendLog);
    connect(m_runner, &TrainingRunner::finished, this, [this](bool ok, const QString &msg) {
        appendLog(QStringLiteral("[结束] %1").arg(msg));
        setRunning(false);
        refreshArtifacts();
        // 进程 exit=0 不等于训练成功（TrainingRunner.h 的既有口径）：用产物存在性说话
        if (m_lastOnnx.isEmpty()) {
            appendLog(ok ? QStringLiteral("[注意] 进程正常结束但未发现 ONNX：训练只产出权重，"
                                          "请在训练机上执行上方导出命令后再回填节点")
                         : QStringLiteral("[注意] 训练没有正常结束，先看日志里的最后一条错误"));
        } else {
            appendLog(QStringLiteral("[产物] 已发现 ONNX：%1").arg(m_lastOnnx));
        }
        // 用户已请求关闭 → 收尾后自动关窗（2026-10-29）：文案说「会自动关闭」就真关
        if (m_closeRequested) {
            m_closeRequested = false;
            accept();
            return;
        }
    });

    loadSettings();
    refreshCommand();
    m_python->setText(m_python->text().isEmpty()
                          ? QStringLiteral("C:\\YOLOX\\.venv\\Scripts\\python.exe")
                          : m_python->text());
    // 无产物前不允许回填（构造期不扫描目录，避免大目录拖慢开窗）
    m_detectBtn->setEnabled(false);
    m_countBtn->setEnabled(false);
}

void TrainingRunnerDialog::loadSettings()
{
    QSettings s;
    m_python->setText(s.value(QStringLiteral("train/python")).toString());
    m_yoloxDir->setText(s.value(QStringLiteral("train/yoloxDir")).toString());
    m_expOrWeights->setText(s.value(QStringLiteral("train/exp")).toString());
    m_dataYaml->setText(s.value(QStringLiteral("train/data")).toString());
    m_trainer->setCurrentIndex(s.value(QStringLiteral("train/trainer"), 0).toInt());
    // 超参也落 QSettings（此前只存 5 个键，现场每次重开都要重填 epochs/imgsz/batch）
    m_ckpt->setText(s.value(QStringLiteral("train/ckpt")).toString());
    m_extra->setText(s.value(QStringLiteral("train/extra")).toString());
    m_epochs->setValue(s.value(QStringLiteral("train/epochs"), 100).toInt());
    m_imgsz->setValue(s.value(QStringLiteral("train/imgsz"), 640).toInt());
    m_batch->setValue(s.value(QStringLiteral("train/batch"), 8).toInt());
    m_device->setCurrentIndex(s.value(QStringLiteral("train/device"), 0).toInt());
}

void TrainingRunnerDialog::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("train/python"), m_python->text().trimmed());
    s.setValue(QStringLiteral("train/yoloxDir"), m_yoloxDir->text().trimmed());
    s.setValue(QStringLiteral("train/exp"), m_expOrWeights->text().trimmed());
    s.setValue(QStringLiteral("train/data"), m_dataYaml->text().trimmed());
    s.setValue(QStringLiteral("train/trainer"), m_trainer->currentIndex());
    s.setValue(QStringLiteral("train/ckpt"), m_ckpt->text().trimmed());
    s.setValue(QStringLiteral("train/extra"), m_extra->text().trimmed());
    s.setValue(QStringLiteral("train/epochs"), m_epochs->value());
    s.setValue(QStringLiteral("train/imgsz"), m_imgsz->value());
    s.setValue(QStringLiteral("train/batch"), m_batch->value());
    s.setValue(QStringLiteral("train/device"), m_device->currentIndex());
}

void TrainingRunnerDialog::onBrowsePython()
{
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("选择 python.exe"),
                                                   QString(), QStringLiteral("python.exe (python.exe)"));
    if (!f.isEmpty())
        m_python->setText(f);
}

void TrainingRunnerDialog::onBrowseYolox()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("选择 YOLOX 源码目录"));
    if (!d.isEmpty())
        m_yoloxDir->setText(d);
}

void TrainingRunnerDialog::onBrowseExp()
{
    const QString f = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 exp 文件或初始权重"), QString(),
        QStringLiteral("实验/权重 (*.py *.pt *.pth);;所有文件 (*)"));
    if (!f.isEmpty())
        m_expOrWeights->setText(f);
}

void TrainingRunnerDialog::onBrowseCkpt()
{
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("选择续训权重"), QString(),
                                                   QStringLiteral("权重 (*.pth *.pt)"));
    if (!f.isEmpty())
        m_ckpt->setText(f);
}

void TrainingRunnerDialog::onProbeEnv()
{
    // 后台探测（2026-10-29 复核 P3-1）：probe 同步等子进程，最长 20 秒；
    // 放 QtConcurrent 里跑，界面只收结果。重复点击时忽略（已在探测中）。
    if (m_probeWatcher && m_probeWatcher->isRunning())
        return;
    const QString python = m_python->text().trimmed();
    m_envLabel->setStyleSheet(QString());
    m_envLabel->setText(QStringLiteral("检测中…（后台进行，界面不会卡住）"));
    if (m_probeBtn)
        m_probeBtn->setEnabled(false);

    if (!m_probeWatcher) {
        m_probeWatcher = new QFutureWatcher<TrainingRunner::EnvStatus>(this);
        connect(m_probeWatcher, &QFutureWatcher<TrainingRunner::EnvStatus>::finished, this, [this]() {
            const TrainingRunner::EnvStatus st = m_probeWatcher->result();
            m_envOk = st.ok;
            m_envLabel->setText(st.ok
                                    ? QStringLiteral("环境就绪：%1").arg(st.detail)
                                    : QStringLiteral("环境不可用：%1（见 train-env/README.md）").arg(st.detail));
            m_envLabel->setStyleSheet(st.ok ? QStringLiteral("color:#1a7f37;")
                                            : QStringLiteral("color:#c22;"));
            if (m_probeBtn)
                m_probeBtn->setEnabled(true);
            setRunning(m_runner && m_runner->running());
            saveSettings();
        });
    }
    m_probeWatcher->setFuture(
        QtConcurrent::run([python]() { return TrainingRunner::probe(python); }));
}

void TrainingRunnerDialog::onStart()
{
    if (m_runner->running())
        return;
    if (!m_envOk) {
        // 环境还没确认（2026-10-29 复核 P3-1）：先起一次后台探测，别把界面冻在这里。
        onProbeEnv();
        QMessageBox::information(
            this, QStringLiteral("先检测训练环境"),
            QStringLiteral("正在后台检测训练环境，结果出来后（标签变绿）再点「开始训练」。\n\n"
                           "产线机不需要装训练环境；训练机上请按 train-env/README.md 安装。"));
        return;
    }
    const QString program = buildProgram();
    const QStringList args = buildArgs();
    if (program.isEmpty()) {
        return;
    }
    saveSettings();
    setRunning(true);
    appendLog(QStringLiteral("[开始] 训练器：%1").arg(m_trainer->currentText()));
    m_runner->start(program, args, m_yoloxDir->text().trimmed());
}

void TrainingRunnerDialog::onCancel()
{
    // cancel() 现在也是非阻塞（2026-10-29）：只发 terminate + 5 秒后强杀兜底
    appendLog(QStringLiteral("[取消] 正在终止训练进程…（约 5 秒后仍未退出会强制结束）"));
    m_runner->cancel();
}

void TrainingRunnerDialog::setRunning(bool on)
{
    m_startBtn->setEnabled(!on && m_envOk);
    m_cancelBtn->setEnabled(on);
    m_probeBtn->setEnabled(!on);
}

QString TrainingRunnerDialog::buildProgram() const
{
    return m_python->text().trimmed();
}

QStringList TrainingRunnerDialog::buildArgs() const
{
    QStringList args;
    const QString python = m_python->text().trimmed();
    if (python.isEmpty())
        return args;
    const bool yolox = (m_trainer->currentIndex() == 0);
    if (yolox) {
        args << QStringLiteral("-m") << QStringLiteral("yolox.tools.train");
        const QString exp = m_expOrWeights->text().trimmed();
        if (exp.isEmpty())
            args << QStringLiteral("-f") << QStringLiteral("exps/default/yolox_s.py");
        else
            args << QStringLiteral("-f") << exp;
        args << QStringLiteral("-d")
             << (m_device->currentIndex() == 0 ? QStringLiteral("-1")
                                               : QString::number(m_device->currentIndex() - 1));
        args << QStringLiteral("-b") << QString::number(m_batch->value());
        if (!m_ckpt->text().trimmed().isEmpty())
            args << QStringLiteral("-c") << m_ckpt->text().trimmed();
        if (!m_extra->text().trimmed().isEmpty())
            args << m_extra->text().trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    } else {
        // Ultralytics 没有稳定 CLI，用 -c 跑一段等价 Python（可读、可复制到命令行）
        const QString weights = m_expOrWeights->text().trimmed().isEmpty()
                                    ? QStringLiteral("yolov8n.pt")
                                    : m_expOrWeights->text().trimmed();
        const QString data = m_dataYaml->text().trimmed();
        const QString dev = m_device->currentIndex() == 0
                                ? QStringLiteral("cpu")
                                : QString::number(m_device->currentIndex() - 1);
        const QString code =
            QStringLiteral("from ultralytics import YOLO\n"
                           "m = YOLO(r'%1')\n"
                           "m.train(data=r'%2', epochs=%3, imgsz=%4, batch=%5, device='%6')")
                .arg(weights)
                .arg(data)
                .arg(m_epochs->value())
                .arg(m_imgsz->value())
                .arg(m_batch->value())
                .arg(dev);
        args << QStringLiteral("-c") << code;
    }
    return args;
}

void TrainingRunnerDialog::refreshCommand()
{
    const QString python = m_python->text().trimmed();
    const QStringList args = buildArgs();
    QString text = python;
    for (const QString &a : args) {
        // 多行代码参数加引号示意，便于人工复制
        text += a.contains(QLatin1Char('\n')) ? QStringLiteral(" \"<多行 python 代码>\"")
                                              : QStringLiteral(" ") + a;
    }
    m_commandPreview->setPlainText(text);
    m_exportPreview->setPlainText(buildExportCommand());
    const bool yolox = (m_trainer->currentIndex() == 0);
    m_expOrWeights->setPlaceholderText(
        yolox ? QStringLiteral("exps/default/yolox_s.py")
              : QStringLiteral("yolov8n.pt（初始权重）"));
    m_dataYaml->setEnabled(!yolox);
    m_epochs->setEnabled(!yolox);
    m_imgsz->setEnabled(!yolox);
}

void TrainingRunnerDialog::closeEvent(QCloseEvent *e)
{
    if (m_runner && m_runner->running()) {
        // 拒绝关闭 + 请求取消：跑了几小时的训练不能被顺手关窗带走（2026-10-28 复核 P3）
        // 非阻塞（2026-10-29）：cancel() 的 waitForFinished 会冻结 GUI 最多 5 秒
        m_runner->requestCancel();
        m_closeRequested = true;
        appendLog(QStringLiteral("已请求停止：训练收尾后窗口会自动关闭"));
        e->ignore();
        return;
    }
    QDialog::closeEvent(e);
}

void TrainingRunnerDialog::accept()
{
    // 与 closeEvent/reject 同一守卫（2026-10-29 P2-3）：训练中不允许"硬关"
    if (m_runner && m_runner->running()) {
        m_runner->requestCancel();
        m_closeRequested = true;
        appendLog(QStringLiteral("已请求停止：训练收尾后窗口会自动关闭"));
        return;
    }
    QDialog::accept();
}

void TrainingRunnerDialog::reject()
{
    if (m_runner && m_runner->running()) {
        m_runner->requestCancel();
        m_closeRequested = true;
        appendLog(QStringLiteral("已请求停止：训练收尾后窗口会自动关闭"));
        return;
    }
    QDialog::reject();
}

QString TrainingRunnerDialog::buildExportCommand() const
{
    const QString python = m_python->text().trimmed();
    const int imgsz = m_imgsz->value();
    if (m_trainer->currentIndex() == 0) {
        // YOLOX：用 exp + ckpt 导出，默认权重路径与官方输出一致
        const QString exp = m_expOrWeights->text().trimmed().isEmpty()
                                ? QStringLiteral("exps/default/yolox_s.py")
                                : m_expOrWeights->text().trimmed();
        QString ckpt = m_ckpt->text().trimmed();
        if (ckpt.isEmpty())
            ckpt = QStringLiteral("YOLOX_outputs/yolox_s/best_ckpt.pth");
        return QStringLiteral("%1 -m yolox.tools.export_onnx -f \"%2\" -c \"%3\" "
                              "--output-name best.onnx --opset 12 --input-size %4,%5")
            .arg(python, exp, ckpt, QString::number(imgsz), QString::number(imgsz));
    }
    // Ultralytics：训练只产出权重，按默认路径生成 export 命令，训练机上可直接复制执行
    QString weights = m_ckpt->text().trimmed();
    if (weights.isEmpty())
        weights = QStringLiteral("runs/detect/train/weights/best.pt");
    return QStringLiteral("%1 -c \"from ultralytics import YOLO; "
                          "YOLO(r'%2').export(format='onnx', imgsz=%3, opset=12)\"")
        .arg(python, weights, QString::number(imgsz));
}

QString TrainingRunnerDialog::discoverOnnx() const
{
    const QString work = m_yoloxDir->text().trimmed();
    if (work.isEmpty() || !QDir(work).exists())
        return QString();
    QDirIterator it(work, QStringList{QStringLiteral("*.onnx")},
                    QDir::Files, QDirIterator::Subdirectories);
    QString best;
    qint64 bestMtime = -1;
    int scanned = 0;
    while (it.hasNext() && scanned < 5000) {
        const QString path = it.next();
        const QFileInfo fi(path);
        if (!fi.isFile())
            continue;
        ++scanned;
        const qint64 mt = fi.lastModified().toMSecsSinceEpoch();
        if (mt > bestMtime) {
            bestMtime = mt;
            best = QDir::toNativeSeparators(path);
        }
    }
    return best;
}

QString TrainingRunnerDialog::resolveClasses(const QString &modelPath) const
{
    if (modelPath.isEmpty())
        return QString();
    const QString near = QFileInfo(modelPath).absolutePath() + QStringLiteral("/classes.txt");
    if (QFileInfo::exists(near))
        return QDir::toNativeSeparators(near);
    const QString yaml = m_dataYaml->text().trimmed();
    if (!yaml.isEmpty()) {
        const QString fromYaml = QFileInfo(yaml).absolutePath() + QStringLiteral("/classes.txt");
        if (QFileInfo::exists(fromYaml))
            return QDir::toNativeSeparators(fromYaml);
    }
    return QString();
}

void TrainingRunnerDialog::refreshArtifacts()
{
    m_lastOnnx = discoverOnnx();
    m_lastClasses = resolveClasses(m_lastOnnx);
    const bool ready = !m_lastOnnx.isEmpty();
    m_detectBtn->setEnabled(ready);
    m_countBtn->setEnabled(ready);
    if (!ready) {
        m_artifactLabel->setText(QStringLiteral("尚未发现 ONNX（训练只产出权重，需先导出）"));
        return;
    }
    m_artifactLabel->setText(m_lastOnnx
        + (m_lastClasses.isEmpty()
               ? QStringLiteral("\n未找到 classes.txt（类别将显示为数字）")
               : QStringLiteral("\n类别表：%1").arg(m_lastClasses)));
}

void TrainingRunnerDialog::onChooseOnnx()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 ONNX 模型"), m_yoloxDir->text().trimmed(),
        QStringLiteral("ONNX 模型 (*.onnx)"));
    if (path.isEmpty())
        return;
    m_lastOnnx = QDir::toNativeSeparators(path);
    m_lastClasses = resolveClasses(m_lastOnnx);
    m_detectBtn->setEnabled(true);
    m_countBtn->setEnabled(true);
    m_artifactLabel->setText(m_lastOnnx
        + (m_lastClasses.isEmpty()
               ? QStringLiteral("\n未找到 classes.txt（类别将显示为数字）")
               : QStringLiteral("\n类别表：%1").arg(m_lastClasses)));
    appendLog(QStringLiteral("[产物] 已选择 ONNX：%1").arg(m_lastOnnx));
}

void TrainingRunnerDialog::onCopyExportCommand()
{
    QApplication::clipboard()->setText(m_exportPreview->toPlainText());
    appendLog(QStringLiteral("[提示] 导出命令已复制到剪贴板"));
}

void TrainingRunnerDialog::onApplyDetect()
{
    if (m_lastOnnx.isEmpty())
        return;
    emit applyModelRequested(QStringLiteral("DnnDetectNode"), m_lastOnnx, m_lastClasses);
    appendLog(QStringLiteral("[提示] 已请求插入 ONNX目标检测（画布锁定时会失败，请看状态栏）"));
}

void TrainingRunnerDialog::onApplyCounting()
{
    if (m_lastOnnx.isEmpty())
        return;
    emit applyModelRequested(QStringLiteral("ObjectCountingNode"), m_lastOnnx, m_lastClasses);
    appendLog(QStringLiteral("[提示] 已请求插入 流水线计数（画布锁定时会失败，请看状态栏）"));
}

void TrainingRunnerDialog::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}
