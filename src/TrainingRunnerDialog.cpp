#include "TrainingRunnerDialog.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
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

    // ── 按钮 ──
    auto *btns = new QHBoxLayout();
    m_startBtn = new QPushButton(QStringLiteral("开始训练"));
    connect(m_startBtn, &QPushButton::clicked, this, &TrainingRunnerDialog::onStart);
    m_cancelBtn = new QPushButton(QStringLiteral("取消训练"));
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TrainingRunnerDialog::onCancel);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
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
        if (!ok)
            appendLog(QStringLiteral("[注意] 训练没有正常结束，先看日志里的最后一条错误"));
    });

    loadSettings();
    refreshCommand();
    m_python->setText(m_python->text().isEmpty()
                          ? QStringLiteral("C:\\YOLOX\\.venv\\Scripts\\python.exe")
                          : m_python->text());
}

void TrainingRunnerDialog::loadSettings()
{
    QSettings s;
    m_python->setText(s.value(QStringLiteral("train/python")).toString());
    m_yoloxDir->setText(s.value(QStringLiteral("train/yoloxDir")).toString());
    m_expOrWeights->setText(s.value(QStringLiteral("train/exp")).toString());
    m_dataYaml->setText(s.value(QStringLiteral("train/data")).toString());
    m_trainer->setCurrentIndex(s.value(QStringLiteral("train/trainer"), 0).toInt());
}

void TrainingRunnerDialog::saveSettings()
{
    QSettings s;
    s.setValue(QStringLiteral("train/python"), m_python->text().trimmed());
    s.setValue(QStringLiteral("train/yoloxDir"), m_yoloxDir->text().trimmed());
    s.setValue(QStringLiteral("train/exp"), m_expOrWeights->text().trimmed());
    s.setValue(QStringLiteral("train/data"), m_dataYaml->text().trimmed());
    s.setValue(QStringLiteral("train/trainer"), m_trainer->currentIndex());
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
    m_envLabel->setText(QStringLiteral("检测中…"));
    const TrainingRunner::EnvStatus st = TrainingRunner::probe(m_python->text().trimmed());
    m_envOk = st.ok;
    m_envLabel->setText(st.ok
                            ? QStringLiteral("环境就绪：%1").arg(st.detail)
                            : QStringLiteral("环境不可用：%1（见 train-env/README.md）").arg(st.detail));
    m_envLabel->setStyleSheet(st.ok ? QStringLiteral("color:#1a7f37;")
                                    : QStringLiteral("color:#c22;"));
    setRunning(m_runner->running());
    saveSettings();
}

void TrainingRunnerDialog::onStart()
{
    if (m_runner->running())
        return;
    if (!m_envOk) {
        // 先自动探一次，避免"忘了点检测"就被拒
        onProbeEnv();
        if (!m_envOk) {
            QMessageBox::warning(this, QStringLiteral("训练环境不可用"),
                                 m_envLabel->text()
                                     + QStringLiteral("\n\n产线机不需要装训练环境；"
                                                      "请在训练机上按 train-env/README.md 安装。"));
            return;
        }
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
    appendLog(QStringLiteral("[取消] 正在终止训练进程…"));
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
    const bool yolox = (m_trainer->currentIndex() == 0);
    m_expOrWeights->setPlaceholderText(
        yolox ? QStringLiteral("exps/default/yolox_s.py")
              : QStringLiteral("yolov8n.pt（初始权重）"));
    m_dataYaml->setEnabled(!yolox);
    m_epochs->setEnabled(!yolox);
    m_imgsz->setEnabled(!yolox);
}

void TrainingRunnerDialog::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}
