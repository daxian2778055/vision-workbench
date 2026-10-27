#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QTableWidget;
class QPushButton;

/// 特征库管理（2026-10-27）：现场要看得到库里有什么——浏览/预览/删除/清空。
/// 之前只能靠节点入库，库里状态不可见，换型排查无从下手。
class FeatureLibraryDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FeatureLibraryDialog(QWidget *parent = nullptr);

private slots:
    void reload();
    void onDeleteSelected();
    void onClearAll();
    void onSelectionChanged();

private:
    void fillTable();

    QComboBox *m_detectorFilter = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
};
