#ifndef GLOBALCAMERADIALOG_H
#define GLOBALCAMERADIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>

class GlobalCameraDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlobalCameraDialog(QWidget *parent = nullptr);
    ~GlobalCameraDialog();

 signals:
    void logMessage(const QString &message);

private slots:
    void onCameraSelectionChanged(int index);
    void onBindDevice();
    void onUnbindDevice();
    void onRefreshDevices();
    void onReadParams();
    void onApplyParams();

private:
    void setupUI();
    void loadCameras();
    void populatePixelFormatComboBox(void *hCamera = nullptr);
    void populateTriggerModeComboBox();
    void populateTriggerSourceComboBox();
    void loadParamsToUI(const QString &cameraName);
    void refreshDeviceList();
    void updateCameraStatus(const QString &cameraName);

    QComboBox *m_cameraListCombo;
    QComboBox *m_deviceComboBox;
    QPushButton *m_bindButton;
    QPushButton *m_unbindButton;
    QPushButton *m_refreshButton;
    QPushButton *m_readParamsButton;
    QPushButton *m_applyParamsButton;
    QLabel *m_cameraStatusLabel;
    
    // 相机参数控件
    QDoubleSpinBox *m_exposureTimeSpinBox;
    QDoubleSpinBox *m_gainSpinBox;
    QDoubleSpinBox *m_frameRateSpinBox;
    QSpinBox *m_widthSpinBox;
    QSpinBox *m_heightSpinBox;
    QComboBox *m_pixelFormatComboBox;
    QComboBox *m_triggerModeComboBox;
    QComboBox *m_triggerSourceComboBox;
};

#endif // GLOBALCAMERADIALOG_H