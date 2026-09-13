#pragma once

#include <QDialog>
#include "HalconWindow.h"

class NodeBase;
class QComboBox;
class QLabel;

/// 在图上框选并按类别保存裁块到 annotations/<类名>/
class AnnotationDialog : public QDialog
{
    Q_OBJECT
public:
    AnnotationDialog(NodeBase *node, const QString &annotationDir, QWidget *parent = nullptr);

    QString annotationDir() const { return m_dir; }

private slots:
    void onDrawBox();
    void onRoiEdited(const RoiShape &shape);
    void refreshClassList();

private:
    void loadImage();

    NodeBase *m_node = nullptr;
    QString m_dir;
    HalconWindow *m_view = nullptr;
    QComboBox *m_classCombo = nullptr;
    QLabel *m_hint = nullptr;
};
