#include "GlobalCameraDialog.h"
#include "GlobalCameraManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QFormLayout>
#include <QDebug>
#include <QComboBox>

// MVS SDK头文件
#include "MvCameraControl.h"
#include "AppLog.h"

GlobalCameraDialog::GlobalCameraDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("MVS相机配置"));
    setMinimumSize(900, 700);
    setupUI();
    loadCameras();
}

GlobalCameraDialog::~GlobalCameraDialog()
{
}

void GlobalCameraDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 全局相机列表和设备绑定区域
    QGroupBox *cameraListGroup = new QGroupBox("全局相机列表");
    QHBoxLayout *cameraListLayout = new QHBoxLayout(cameraListGroup);
    
    // 左侧：全局相机列表（8个相机）
    QVBoxLayout *leftLayout = new QVBoxLayout();
    QLabel *cameraListLabel = new QLabel("选择全局相机:");
    m_cameraListCombo = new QComboBox();
    m_cameraListCombo->setMinimumWidth(150);
    
    // 填充8个全局相机
    for (int i = 1; i <= 8; i++) {
        QString cameraName = QString("Camera%1").arg(i);
        m_cameraListCombo->addItem(cameraName, cameraName);
    }
    
    leftLayout->addWidget(cameraListLabel);
    leftLayout->addWidget(m_cameraListCombo);
    
    // 中间：设备绑定区域
    QVBoxLayout *centerLayout = new QVBoxLayout();
    QLabel *deviceListLabel = new QLabel("绑定设备:");
    m_deviceComboBox = new QComboBox();
    m_deviceComboBox->setMinimumWidth(250);
    
    centerLayout->addWidget(deviceListLabel);
    centerLayout->addWidget(m_deviceComboBox);
    
    // 右侧：操作按钮
    QVBoxLayout *rightLayout = new QVBoxLayout();
    m_bindButton = new QPushButton("绑定设备");
    m_unbindButton = new QPushButton("解除绑定");
    m_refreshButton = new QPushButton("刷新设备");
    
    rightLayout->addWidget(m_bindButton);
    rightLayout->addWidget(m_unbindButton);
    rightLayout->addWidget(m_refreshButton);
    
    cameraListLayout->addLayout(leftLayout);
    cameraListLayout->addLayout(centerLayout);
    cameraListLayout->addLayout(rightLayout);
    
    mainLayout->addWidget(cameraListGroup);

    // 相机参数部分
    QGroupBox *paramsGroupBox = new QGroupBox("相机参数");
    QFormLayout *paramsLayout = new QFormLayout(paramsGroupBox);

    m_exposureTimeSpinBox = new QDoubleSpinBox();
    m_exposureTimeSpinBox->setRange(1, 10000000);
    m_exposureTimeSpinBox->setSuffix(" μs");
    m_exposureTimeSpinBox->setDecimals(0);

    m_gainSpinBox = new QDoubleSpinBox();
    m_gainSpinBox->setRange(0, 100);
    m_gainSpinBox->setSuffix(" dB");
    m_gainSpinBox->setDecimals(2);

    m_frameRateSpinBox = new QDoubleSpinBox();
    m_frameRateSpinBox->setRange(0.1, 1000);
    m_frameRateSpinBox->setSuffix(" fps");
    m_frameRateSpinBox->setDecimals(2);

    m_pixelFormatComboBox = new QComboBox();
    populatePixelFormatComboBox();

    m_triggerModeComboBox = new QComboBox();
    populateTriggerModeComboBox();

    m_triggerSourceComboBox = new QComboBox();
    populateTriggerSourceComboBox();

    m_widthSpinBox = new QSpinBox();
    m_widthSpinBox->setRange(1, 10000);
    m_widthSpinBox->setReadOnly(true);
    
    m_heightSpinBox = new QSpinBox();
    m_heightSpinBox->setRange(1, 10000);
    m_heightSpinBox->setReadOnly(true);

    m_cameraStatusLabel = new QLabel("未绑定设备");
    m_cameraStatusLabel->setStyleSheet("color: gray; font-weight: bold;");

    m_readParamsButton = new QPushButton("读取参数");
    m_applyParamsButton = new QPushButton("应用参数");

    paramsLayout->addRow("相机状态:", m_cameraStatusLabel);
    paramsLayout->addRow("曝光时间:", m_exposureTimeSpinBox);
    paramsLayout->addRow("增益:", m_gainSpinBox);
    paramsLayout->addRow("帧率:", m_frameRateSpinBox);
    paramsLayout->addRow("图像宽度:", m_widthSpinBox);
    paramsLayout->addRow("图像高度:", m_heightSpinBox);
    paramsLayout->addRow("像素格式:", m_pixelFormatComboBox);
    paramsLayout->addRow("触发模式:", m_triggerModeComboBox);
    paramsLayout->addRow("触发源:", m_triggerSourceComboBox);
    
    QHBoxLayout *paramsButtonLayout = new QHBoxLayout();
    paramsButtonLayout->addWidget(m_readParamsButton);
    paramsButtonLayout->addWidget(m_applyParamsButton);
    paramsLayout->addRow(paramsButtonLayout);

    mainLayout->addWidget(paramsGroupBox);

    // 连接信号
    connect(m_cameraListCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GlobalCameraDialog::onCameraSelectionChanged);
    connect(m_bindButton, &QPushButton::clicked, this, &GlobalCameraDialog::onBindDevice);
    connect(m_unbindButton, &QPushButton::clicked, this, &GlobalCameraDialog::onUnbindDevice);
    connect(m_refreshButton, &QPushButton::clicked, this, &GlobalCameraDialog::onRefreshDevices);
    connect(m_readParamsButton, &QPushButton::clicked, this, &GlobalCameraDialog::onReadParams);
    connect(m_applyParamsButton, &QPushButton::clicked, this, &GlobalCameraDialog::onApplyParams);
}

void GlobalCameraDialog::loadCameras()
{
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    auto cameras = manager->cameras();
    
    // 更新相机列表的显示
    for (int i = 0; i < m_cameraListCombo->count(); i++) {
        QString cameraName = m_cameraListCombo->itemData(i).toString();
        if (cameras.contains(cameraName)) {
            const auto& camera = cameras[cameraName];
            QString displayText = cameraName;
            if (!camera.deviceName.isEmpty()) {
                if (manager->isCameraOpen(cameraName)) {
                    displayText += " [已打开]";
                } else {
                    displayText += " [已绑定]";
                }
            }
            m_cameraListCombo->setItemText(i, displayText);
        }
    }
}

void GlobalCameraDialog::populatePixelFormatComboBox(void *hCamera)
{
    m_pixelFormatComboBox->clear();
    auto formats = GlobalCameraManager::getSupportedPixelFormats(hCamera);
    for (const auto &pair : formats) {
        m_pixelFormatComboBox->addItem(pair.first, static_cast<qlonglong>(pair.second));
    }
}

void GlobalCameraDialog::populateTriggerModeComboBox()
{
    m_triggerModeComboBox->clear();
    m_triggerModeComboBox->addItem("Off");
    m_triggerModeComboBox->addItem("On");
}

void GlobalCameraDialog::populateTriggerSourceComboBox()
{
    m_triggerSourceComboBox->clear();
    m_triggerSourceComboBox->addItem("Line0");
    m_triggerSourceComboBox->addItem("Line1");
    m_triggerSourceComboBox->addItem("Line2");
    m_triggerSourceComboBox->addItem("Line3");
    m_triggerSourceComboBox->addItem("Software");
}

void GlobalCameraDialog::loadParamsToUI(const QString &cameraName)
{
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    auto camera = manager->getCamera(cameraName);
    
    m_exposureTimeSpinBox->setValue(camera.params.exposureTime);
    m_gainSpinBox->setValue(camera.params.gain);
    m_frameRateSpinBox->setValue(camera.params.frameRate);
    m_widthSpinBox->setValue(camera.params.width);
    m_heightSpinBox->setValue(camera.params.height);
    
    int pixelIndex = m_pixelFormatComboBox->findText(camera.params.pixelFormat);
    if (pixelIndex >= 0) {
        m_pixelFormatComboBox->setCurrentIndex(pixelIndex);
    }
    
    int triggerIndex = m_triggerModeComboBox->findText(camera.params.triggerMode);
    if (triggerIndex >= 0) {
        m_triggerModeComboBox->setCurrentIndex(triggerIndex);
    }
    
    int sourceIndex = m_triggerSourceComboBox->findText(camera.params.triggerSource);
    if (sourceIndex >= 0) {
        m_triggerSourceComboBox->setCurrentIndex(sourceIndex);
    }
}

void GlobalCameraDialog::onCameraSelectionChanged(int index)
{
    if (index < 0) {
        return;
    }
    
    QString cameraName = m_cameraListCombo->itemData(index).toString();
    VFP_DEBUG << "选择相机:" << cameraName;
    
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    
    // 动态获取该相机支持的像素格式（使用已打开的 MVS 句柄）
    void *hCamera = manager->getCameraHandle(cameraName);
    populatePixelFormatComboBox(hCamera);
    
    // 更新相机状态显示
    updateCameraStatus(cameraName);
    
    // 加载相机参数到UI
    loadParamsToUI(cameraName);
    
    // 刷新设备列表
    refreshDeviceList();
}

void GlobalCameraDialog::onBindDevice()
{
    QString cameraName = m_cameraListCombo->currentData().toString();
    QString deviceName = m_deviceComboBox->currentText();
    
    if (deviceName.isEmpty() || deviceName == "无可用设备") {
        QMessageBox::warning(this, "警告", "请选择一个设备");
        return;
    }
    
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    
    // 设置设备绑定
    if (!manager->setCameraDevice(cameraName, deviceName)) {
        QMessageBox::warning(this, "警告", "设备绑定失败，可能设备已被其他相机占用");
        return;
    }
    
    // 打开相机
    if (!manager->openCamera(cameraName)) {
        QMessageBox::warning(this, "警告", "相机打开失败");
        return;
    }

    // 先根据相机句柄刷新像素格式下拉列表（使枚举值与实际相机支持的对应）
    void *hCamera = manager->getCameraHandle(cameraName);
    populatePixelFormatComboBox(hCamera);
    
    // 更新UI
    updateCameraStatus(cameraName);
    loadParamsToUI(cameraName);
    loadCameras();
    
    emit logMessage(QString("%1 已绑定并打开设备: %2").arg(cameraName).arg(deviceName));
}

void GlobalCameraDialog::onUnbindDevice()
{
    QString cameraName = m_cameraListCombo->currentData().toString();
    
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    
    // 关闭相机
    if (manager->isCameraOpen(cameraName)) {
        manager->closeCamera(cameraName);
    }
    
    // 移除设备绑定
    manager->removeCameraDevice(cameraName);
    
    // 更新UI
    updateCameraStatus(cameraName);
    loadParamsToUI(cameraName);
    loadCameras();
    
    emit logMessage(QString("%1 已解除设备绑定").arg(cameraName));
}

void GlobalCameraDialog::onRefreshDevices()
{
    refreshDeviceList();

    const QString cameraName = m_cameraListCombo->currentData().toString();
    GlobalCameraManager *manager = GlobalCameraManager::instance();

    // 当前全局相机已绑定且已打开时，与「读取参数」一致：从设备读入并刷新面板
    if (manager->isCameraBound(cameraName) && manager->isCameraOpen(cameraName)) {
        if (manager->readCameraParams(cameraName)) {
            loadParamsToUI(cameraName);
            updateCameraStatus(cameraName);
            loadCameras();
            emit logMessage(QStringLiteral("设备列表已刷新，%1 已从设备同步参数").arg(cameraName));
            return;
        }
    }

    emit logMessage(QStringLiteral("设备列表已刷新"));
}

void GlobalCameraDialog::refreshDeviceList()
{
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    QStringList devices = manager->getAvailableDevices();
    
    QString currentCameraName = m_cameraListCombo->currentData().toString();
    bool isCurrentCameraBound = manager->isCameraBound(currentCameraName);
    
    m_deviceComboBox->clear();
    
    if (devices.isEmpty()) {
        m_deviceComboBox->addItem("无可用设备");
        return;
    }
    
    // 添加可用设备，并标记已被占用的设备
    for (const QString &device : devices) {
        QString displayText = device;
        
        // 检查设备是否已被其他相机占用
        if (isCurrentCameraBound && manager->getCamera(currentCameraName).deviceName == device) {
            displayText += " [当前相机]";
        } else if (manager->isDeviceUsedByOtherCamera(device)) {
            displayText += " [已被占用]";
        }
        
        m_deviceComboBox->addItem(displayText, device);
    }
}

void GlobalCameraDialog::updateCameraStatus(const QString &cameraName)
{
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    
    if (!manager->isCameraBound(cameraName)) {
        m_cameraStatusLabel->setText("未绑定设备");
        m_cameraStatusLabel->setStyleSheet("color: gray; font-weight: bold;");
        m_bindButton->setEnabled(true);
        m_unbindButton->setEnabled(false);
    } else if (manager->isCameraOpen(cameraName)) {
        m_cameraStatusLabel->setText("已打开");
        m_cameraStatusLabel->setStyleSheet("color: green; font-weight: bold;");
        m_bindButton->setEnabled(false);
        m_unbindButton->setEnabled(true);
    } else {
        m_cameraStatusLabel->setText("已绑定（未打开）");
        m_cameraStatusLabel->setStyleSheet("color: orange; font-weight: bold;");
        m_bindButton->setEnabled(true);
        m_unbindButton->setEnabled(true);
    }
}

void GlobalCameraDialog::onReadParams()
{
    QString cameraName = m_cameraListCombo->currentData().toString();
    
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    
    if (!manager->isCameraOpen(cameraName)) {
        QMessageBox::warning(this, "警告", "相机未打开，无法读取参数");
        return;
    }
    
    if (manager->isCameraOpen(cameraName)) {
        // 重新获取相机支持的像素格式
        void *hCamera = manager->getCameraHandle(cameraName);
        populatePixelFormatComboBox(hCamera);
    }

    if (manager->readCameraParams(cameraName)) {
        loadParamsToUI(cameraName);
        emit logMessage(QString("%1 参数读取成功").arg(cameraName));
    } else {
        QMessageBox::warning(this, "警告", "参数读取失败");
    }
}

void GlobalCameraDialog::onApplyParams()
{
    QString cameraName = m_cameraListCombo->currentData().toString();
    
    GlobalCameraManager *manager = GlobalCameraManager::instance();
    
    if (!manager->isCameraOpen(cameraName)) {
        QMessageBox::warning(this, "警告", "相机未打开，无法应用参数");
        return;
    }
    
    // 获取参数值
    CameraParams params;
    params.exposureTime = m_exposureTimeSpinBox->value();
    params.gain = m_gainSpinBox->value();
    params.frameRate = m_frameRateSpinBox->value();
    params.pixelFormat = m_pixelFormatComboBox->currentText();
    params.triggerMode = m_triggerModeComboBox->currentText();
    params.triggerSource = m_triggerSourceComboBox->currentText();
    
    // 写入参数（写入前先写入像素格式，直接使用整数枚举值确保写入正确）
    void *hCamera = manager->getCameraHandle(cameraName);
    if (hCamera) {
        unsigned int pfVal = static_cast<unsigned int>(m_pixelFormatComboBox->currentData().toULongLong());
        if (pfVal != 0) {
            GlobalCameraManager::writePixelFormatValue(hCamera, pfVal);
        }
    }
    
    // 设置参数（含内存缓存更新）
    manager->setCameraParams(cameraName, params);
    
    emit logMessage(QString("%1 参数应用成功").arg(cameraName));
}