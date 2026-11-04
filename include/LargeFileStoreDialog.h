#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QTableWidget;
class QPushButton;

/// 采集策略（对标 VisionMaster 4.4 手册 11.5）：
/// 大文件存图开关 / 保存路径 / 每批张数 + 已生成 .dat 的解包。
class LargeFileStoreDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LargeFileStoreDialog(QWidget *parent = nullptr);

private:
    void loadSettingsToUi();
    void applySettings();
    void refreshBatchList();
    void refreshPendingLabel();
    void extractSelected();

    QCheckBox *m_enabledCheck = nullptr;
    QLineEdit *m_dirEdit = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QSpinBox *m_batchSpin = nullptr;
    QLabel *m_pendingLabel = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_extractBtn = nullptr;
};
