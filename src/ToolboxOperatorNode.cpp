#include "ToolboxOperatorNode.h"
#include "AppLog.h"
#include <QtGlobal>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace HalconCpp;

ToolboxOperatorNode::ToolboxOperatorNode(QObject *parent, ToolboxBuiltin kind)
    : HalconNode(parent)
    , m_kind(kind)
{
    switch (kind) {
    case ToolboxBuiltin::Blur:
        setName(QStringLiteral("模糊"));
        m_type = IMAGE_PROCESSING;
        break;
    case ToolboxBuiltin::Threshold:
        setName(QStringLiteral("阈值"));
        m_type = IMAGE_PROCESSING;
        break;
    case ToolboxBuiltin::BlobAnalysis:
        setName(QStringLiteral("Blob分析"));
        m_type = SHAPE_ANALYSIS;
        break;
    case ToolboxBuiltin::EdgeDetection:
        setName(QStringLiteral("边缘检测"));
        m_type = SHAPE_ANALYSIS;
        break;
    case ToolboxBuiltin::Area:
        setName(QStringLiteral("面积"));
        m_type = MEASUREMENT;
        break;
    case ToolboxBuiltin::Distance:
        setName(QStringLiteral("距离"));
        m_type = MEASUREMENT;
        break;
    case ToolboxBuiltin::Conditional:
        setName(QStringLiteral("条件判断"));
        m_type = LOGIC;
        break;
    case ToolboxBuiltin::Loop:
        setName(QStringLiteral("循环"));
        m_type = LOGIC;
        break;
    case ToolboxBuiltin::DisplaySink:
        setName(QStringLiteral("显示"));
        m_type = OUTPUT;
        break;
    case ToolboxBuiltin::WriteFile:
        setName(QStringLiteral("写入文件"));
        m_type = OUTPUT;
        break;
    }
}

HImage ToolboxOperatorNode::ensureGray(const HObject &input)
{
    HImage in(input);
    if (!in.IsInitialized()) {
        return HImage();
    }
    HTuple ch;
    CountChannels(in, &ch);
    if (ch.I() > 1) {
        HObject g;
        Rgb1ToGray(in, &g);
        return HImage(g);
    }
    return in;
}

void ToolboxOperatorNode::init()
{
    HalconNode::init();

    switch (m_kind) {
    case ToolboxBuiltin::Blur:
        m_params[QStringLiteral("gaussSize")] = 5;
        break;
    case ToolboxBuiltin::Threshold:
        m_params[QStringLiteral("minGray")] = 128.0;
        m_params[QStringLiteral("maxGray")] = 255.0;
        break;
    case ToolboxBuiltin::BlobAnalysis:
        m_params[QStringLiteral("minGray")] = 128.0;
        m_params[QStringLiteral("maxGray")] = 255.0;
        m_params[QStringLiteral("minArea")] = 100.0;
        break;
    case ToolboxBuiltin::EdgeDetection:
        m_params[QStringLiteral("filterSize")] = 3;
        break;
    case ToolboxBuiltin::Area:
        m_params[QStringLiteral("minGray")] = 128.0;
        m_params[QStringLiteral("maxGray")] = 255.0;
        break;
    case ToolboxBuiltin::Distance:
        m_params[QStringLiteral("row1")] = 0.0;
        m_params[QStringLiteral("col1")] = 0.0;
        m_params[QStringLiteral("row2")] = 100.0;
        m_params[QStringLiteral("col2")] = 100.0;
        break;
    case ToolboxBuiltin::WriteFile:
        m_params[QStringLiteral("filePath")] = QStringLiteral("output.png");
        break;
    default:
        break;
    }
}

void ToolboxOperatorNode::run(bool /*autoSwitch*/)
{
    try {
        HImage gray = ensureGray(m_inputImage);
        if (!gray.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }

        switch (m_kind) {
        case ToolboxBuiltin::Blur: {
            const int size = m_params.value(QStringLiteral("gaussSize"), 5).toInt();
            int ms = qBound(3, size | 1, 11);
            if (ms != 3 && ms != 5 && ms != 7 && ms != 9 && ms != 11)
                ms = 5;
            HObject out;
            GaussImage(gray, &out, HalconCpp::HTuple(ms));
            m_outputImage = out;
            break;
        }
        case ToolboxBuiltin::Threshold: {
            const HalconCpp::HTuple minG = m_params.value(QStringLiteral("minGray"), 128.0).toDouble();
            const HalconCpp::HTuple maxG = m_params.value(QStringLiteral("maxGray"), 255.0).toDouble();
            HObject region;
            Threshold(gray, &region, minG, maxG);
            HTuple w, h;
            GetImageSize(gray, &w, &h);
            HObject bin;
            RegionToBin(region, &bin, HalconCpp::HTuple(255), HalconCpp::HTuple(0), w, h);
            m_outputImage = bin;
            break;
        }
        case ToolboxBuiltin::EdgeDetection: {
            const int fs = m_params.value(QStringLiteral("filterSize"), 3).toInt();
            int ms = qBound(3, fs | 1, 39);
            HObject edgeAmp;
            SobelAmp(gray, &edgeAmp, "sum_abs", ms);
            m_outputImage = edgeAmp;
            break;
        }
        case ToolboxBuiltin::BlobAnalysis: {
            const HalconCpp::HTuple minG = m_params.value(QStringLiteral("minGray"), 128.0).toDouble();
            const HalconCpp::HTuple maxG = m_params.value(QStringLiteral("maxGray"), 255.0).toDouble();
            const double minArea = m_params.value(QStringLiteral("minArea"), 100.0).toDouble();
            HObject region, connected, selected;
            Threshold(gray, &region, minG, maxG);
            Connection(region, &connected);
            SelectShape(connected, &selected, "area", "and", HalconCpp::HTuple(minArea),
                        HalconCpp::HTuple(1e12));
            HTuple w, h;
            GetImageSize(gray, &w, &h);
            HTuple n;
            CountObj(selected, &n);
            const int rc = static_cast<int>(n.D());
            m_params[QStringLiteral("regionCount")] = rc;
            if (rc <= 0) {
                HObject canvas;
                GenImageConst(&canvas, "byte", w, h);
                m_outputImage = canvas;
                break;
            }
            HObject united;
            Union1(selected, &united);
            HObject bin;
            RegionToBin(united, &bin, HalconCpp::HTuple(255), HalconCpp::HTuple(0), w, h);
            m_outputImage = bin;
            break;
        }
        case ToolboxBuiltin::Area: {
            const HalconCpp::HTuple minG = m_params.value(QStringLiteral("minGray"), 128.0).toDouble();
            const HalconCpp::HTuple maxG = m_params.value(QStringLiteral("maxGray"), 255.0).toDouble();
            HObject region, connected, selected;
            Threshold(gray, &region, minG, maxG);
            Connection(region, &connected);
            SelectShape(connected, &selected, "area", "and", HalconCpp::HTuple(10.0),
                        HalconCpp::HTuple(1e12));
            HTuple nObj;
            CountObj(selected, &nObj);
            HTuple w, h;
            GetImageSize(gray, &w, &h);
            const int nc = static_cast<int>(nObj.D());
            if (nc <= 0) {
                HObject canvas;
                GenImageConst(&canvas, "byte", w, h);
                m_params[QStringLiteral("totalArea")] = 0.0;
                m_outputImage = canvas;
                break;
            }
            HObject united;
            Union1(selected, &united);
            HTuple areaSum, rr, cc;
            AreaCenter(united, &areaSum, &rr, &cc);
            m_params[QStringLiteral("totalArea")] = areaSum.D();
            m_params[QStringLiteral("regionCentroidRow")] = rr.D();
            m_params[QStringLiteral("regionCentroidCol")] = cc.D();
            HObject bin;
            RegionToBin(united, &bin, HalconCpp::HTuple(220), HalconCpp::HTuple(0), w, h);
            m_outputImage = bin;
            break;
        }
        case ToolboxBuiltin::Distance: {
            HalconCpp::HTuple iw, ih;
            GetImageSize(gray, &iw, &ih);
            double r1 = m_params.value(QStringLiteral("row1")).toDouble();
            double c1 = m_params.value(QStringLiteral("col1")).toDouble();
            double r2 = m_params.value(QStringLiteral("row2")).toDouble();
            double c2 = m_params.value(QStringLiteral("col2")).toDouble();
            if (r1 == 0.0 && c1 == 0.0 && r2 == 100.0 && c2 == 100.0) {
                r1 = ih.D() / 4.0;
                c1 = iw.D() / 4.0;
                r2 = ih.D() * 3.0 / 4.0;
                c2 = iw.D() * 3.0 / 4.0;
            }
            HalconCpp::HTuple dist;
            DistancePp(r1, c1, r2, c2, &dist);
            m_params[QStringLiteral("distancePixel")] = dist.D();
            m_outputImage = gray;
            break;
        }
        case ToolboxBuiltin::Conditional:
        case ToolboxBuiltin::Loop:
        case ToolboxBuiltin::DisplaySink:
            m_outputImage = gray;
            m_params[QStringLiteral("note")] =
                QStringLiteral("画布与执行器负责执行；本节点输出与输入同源灰度图占位。");
            break;
        case ToolboxBuiltin::WriteFile: {
            const QString path = m_params.value(QStringLiteral("filePath")).toString();
            WriteImage(gray, HalconCpp::HTuple("png"), HalconCpp::HTuple(0),
                       HalconCpp::HTuple(path.toLocal8Bit().constData()));
            m_outputImage = gray;
            m_params[QStringLiteral("lastSavedPath")] = path;
            break;
        }
        default:
            m_outputImage = gray;
            break;
        }
    } catch (const HalconCpp::HException &e) {
        VFP_DEBUG << "ToolboxOperatorNode Halcon error:" << e.ErrorMessage().TextA();
        m_outputImage.Clear();
    } catch (const std::exception &e) {
        VFP_DEBUG << "ToolboxOperatorNode exception:" << e.what();
        m_outputImage.Clear();
    }
}

QWidget *ToolboxOperatorNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *outer = new QVBoxLayout(panel);

    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(name()));
    outer->addWidget(title);

    auto addForm = [outer](const QString &boxTitle) {
        auto *box = new QGroupBox(boxTitle);
        auto *form = new QFormLayout(box);
        outer->addWidget(box);
        return form;
    };

    switch (m_kind) {
    case ToolboxBuiltin::Blur: {
        QFormLayout *form = addForm(QStringLiteral("高斯模糊"));
        auto *combo = new QComboBox(panel);
        combo->setObjectName(QStringLiteral("t_blur_mask"));
        for (int s : {3, 5, 7, 9, 11}) {
            combo->addItem(QString::number(s), s);
        }
        {
            const int g = m_params.value(QStringLiteral("gaussSize"), 5).toInt();
            const int idx = combo->findData(qBound(3, g | 1, 11));
            QSignalBlocker br(combo);
            if (idx >= 0) {
                combo->setCurrentIndex(idx);
            }
        }
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo](int) {
            setParam(QStringLiteral("gaussSize"), combo->currentData().toInt());
        });
        form->addRow(QStringLiteral("核大小"), combo);
        break;
    }
    case ToolboxBuiltin::Threshold:
    case ToolboxBuiltin::Area: {
        const bool area = (m_kind == ToolboxBuiltin::Area);
        QFormLayout *form = addForm(area ? QStringLiteral("面积测量（阈值分割）")
                                         : QStringLiteral("阈值分割"));
        auto *minS = new QSpinBox(panel);
        minS->setObjectName(QStringLiteral("t_gray_min"));
        minS->setRange(0, 255);
        auto *maxS = new QSpinBox(panel);
        maxS->setObjectName(QStringLiteral("t_gray_max"));
        maxS->setRange(0, 255);
        {
            QSignalBlocker bm(minS), bM(maxS);
            minS->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
            maxS->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
        }
        connect(minS, qOverload<int>(&QSpinBox::valueChanged), this, [this, minS, maxS](int v) {
            setParam(QStringLiteral("minGray"), v);
            if (maxS->value() < v) {
                QSignalBlocker b(maxS);
                maxS->setValue(v);
            }
            setParam(QStringLiteral("maxGray"), maxS->value());
        });
        connect(maxS, qOverload<int>(&QSpinBox::valueChanged), this, [this, minS, maxS](int v) {
            setParam(QStringLiteral("maxGray"), v);
            if (minS->value() > v) {
                QSignalBlocker b(minS);
                minS->setValue(v);
            }
            setParam(QStringLiteral("minGray"), minS->value());
        });
        form->addRow(QStringLiteral("灰度下限"), minS);
        form->addRow(QStringLiteral("灰度上限"), maxS);
        break;
    }
    case ToolboxBuiltin::BlobAnalysis: {
        QFormLayout *form = addForm(QStringLiteral("Blob 分析"));
        auto *minS = new QSpinBox(panel);
        minS->setObjectName(QStringLiteral("t_blob_gray_min"));
        minS->setRange(0, 255);
        auto *maxS = new QSpinBox(panel);
        maxS->setObjectName(QStringLiteral("t_blob_gray_max"));
        maxS->setRange(0, 255);
        auto *area = new QDoubleSpinBox(panel);
        area->setObjectName(QStringLiteral("t_blob_min_area"));
        area->setRange(1.0, 1e9);
        area->setDecimals(0);
        area->setSingleStep(10.0);
        {
            QSignalBlocker bm(minS), bM(maxS), ba(area);
            minS->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
            maxS->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
            area->setValue(m_params.value(QStringLiteral("minArea"), 100.0).toDouble());
        }
        connect(minS, qOverload<int>(&QSpinBox::valueChanged), this, [this, minS, maxS, area]() {
            setParam(QStringLiteral("minGray"), minS->value());
            setParam(QStringLiteral("maxGray"), maxS->value());
            setParam(QStringLiteral("minArea"), area->value());
        });
        connect(maxS, qOverload<int>(&QSpinBox::valueChanged), this, [this, minS, maxS, area]() {
            setParam(QStringLiteral("minGray"), minS->value());
            setParam(QStringLiteral("maxGray"), maxS->value());
            setParam(QStringLiteral("minArea"), area->value());
        });
        connect(area, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, minS, maxS, area]() {
            setParam(QStringLiteral("minGray"), minS->value());
            setParam(QStringLiteral("maxGray"), maxS->value());
            setParam(QStringLiteral("minArea"), area->value());
        });
        form->addRow(QStringLiteral("灰度下限"), minS);
        form->addRow(QStringLiteral("灰度上限"), maxS);
        form->addRow(QStringLiteral("最小面积(像素)"), area);
        break;
    }
    case ToolboxBuiltin::EdgeDetection: {
        QFormLayout *form = addForm(QStringLiteral("边缘检测 Sobel"));
        auto *combo = new QComboBox(panel);
        combo->setObjectName(QStringLiteral("t_edge_filt"));
        for (int s = 3; s <= 39; s += 2) {
            combo->addItem(QString::number(s), s);
        }
        {
            const int fs = m_params.value(QStringLiteral("filterSize"), 3).toInt();
            const int idx = combo->findData(qBound(3, fs | 1, 39));
            QSignalBlocker br(combo);
            if (idx >= 0) {
                combo->setCurrentIndex(idx);
            }
        }
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo](int) {
            setParam(QStringLiteral("filterSize"), combo->currentData().toInt());
        });
        form->addRow(QStringLiteral("滤波器尺寸"), combo);
        break;
    }
    case ToolboxBuiltin::Distance: {
        QFormLayout *form = addForm(QStringLiteral("两点距离（像素）"));
        const QString objs[] = {QStringLiteral("t_dst_r1"), QStringLiteral("t_dst_c1"),
                                  QStringLiteral("t_dst_r2"), QStringLiteral("t_dst_c2")};
        const QString lbls[] = {QStringLiteral("起点行"), QStringLiteral("起点列"),
                                  QStringLiteral("终点行"), QStringLiteral("终点列")};
        const QString keys[] = {QStringLiteral("row1"), QStringLiteral("col1"), QStringLiteral("row2"),
                                  QStringLiteral("col2")};
        const double defs[] = {0.0, 0.0, 100.0, 100.0};
        constexpr double mx = 1e7;
        for (int i = 0; i < 4; ++i) {
            auto *sp = new QDoubleSpinBox(panel);
            sp->setObjectName(objs[i]);
            sp->setRange(0.0, mx);
            sp->setDecimals(2);
            {
                QSignalBlocker b(sp);
                sp->setValue(m_params.value(keys[i], defs[i]).toDouble());
            }
            connect(sp, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, panel]() {
                if (auto *w = panel->findChild<QDoubleSpinBox *>(QStringLiteral("t_dst_r1"))) {
                    setParam(QStringLiteral("row1"), w->value());
                }
                if (auto *w = panel->findChild<QDoubleSpinBox *>(QStringLiteral("t_dst_c1"))) {
                    setParam(QStringLiteral("col1"), w->value());
                }
                if (auto *w = panel->findChild<QDoubleSpinBox *>(QStringLiteral("t_dst_r2"))) {
                    setParam(QStringLiteral("row2"), w->value());
                }
                if (auto *w = panel->findChild<QDoubleSpinBox *>(QStringLiteral("t_dst_c2"))) {
                    setParam(QStringLiteral("col2"), w->value());
                }
            });
            form->addRow(lbls[i], sp);
        }
        outer->addWidget(new QLabel(QStringLiteral(
            "当参数为默认值 (0,0)-(100,100) 时，单次运行将改用图像对角线两端点估算距离。")));
        break;
    }
    case ToolboxBuiltin::Conditional:
    case ToolboxBuiltin::Loop:
    case ToolboxBuiltin::DisplaySink:
        outer->addWidget(new QLabel(QStringLiteral(
            "本节点透传上游图像。\n可视化与编排逻辑依赖流程执行与大屏预览；此处不设参数。")));
        break;
    case ToolboxBuiltin::WriteFile: {
        auto *hb = new QHBoxLayout();
        auto *le = new QLineEdit(panel);
        le->setObjectName(QStringLiteral("t_write_path"));
        le->setText(m_params.value(QStringLiteral("filePath"), QStringLiteral("output.png")).toString());
        auto *browse = new QPushButton(QStringLiteral("浏览…"), panel);
        hb->addWidget(le, 1);
        hb->addWidget(browse);
        outer->addLayout(hb);
        connect(le, &QLineEdit::editingFinished, this, [this, le]() {
            setParam(QStringLiteral("filePath"), le->text());
        });
        connect(browse, &QPushButton::clicked, this, [this, panel, le]() {
            QString p = QFileDialog::getSaveFileName(panel->window(),
                                                     QStringLiteral("保存图像"),
                                                     le->text(),
                                                     QStringLiteral("PNG (*.png);;所有文件 (*)"));
            if (!p.isEmpty()) {
                le->setText(p);
                setParam(QStringLiteral("filePath"), p);
            }
        });
        outer->addWidget(new QLabel(QStringLiteral("执行时使用 Halcon WriteImage（PNG）。确保路径可写。")));
        break;
    }
    default:
        outer->addWidget(new QLabel(QStringLiteral("暂无专用参数")));
        break;
    }

    outer->addStretch(1);
    return panel;
}

void ToolboxOperatorNode::updateParamPanel(QWidget *panel)
{
    if (!panel) {
        return;
    }

    switch (m_kind) {
    case ToolboxBuiltin::Blur:
        if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("t_blur_mask"))) {
            QSignalBlocker g(cb);
            const int gv = qBound(3, m_params.value(QStringLiteral("gaussSize"), 5).toInt() | 1, 11);
            const int idx = cb->findData(gv);
            if (idx >= 0) {
                cb->setCurrentIndex(idx);
            }
        }
        break;
    case ToolboxBuiltin::Threshold:
    case ToolboxBuiltin::Area:
        if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("t_gray_min"))) {
            QSignalBlocker b(w);
            w->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
        }
        if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("t_gray_max"))) {
            QSignalBlocker b(w);
            w->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
        }
        break;
    case ToolboxBuiltin::BlobAnalysis:
        if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("t_blob_gray_min"))) {
            QSignalBlocker b(w);
            w->setValue(m_params.value(QStringLiteral("minGray"), 128).toInt());
        }
        if (auto *w = panel->findChild<QSpinBox *>(QStringLiteral("t_blob_gray_max"))) {
            QSignalBlocker b(w);
            w->setValue(m_params.value(QStringLiteral("maxGray"), 255).toInt());
        }
        if (auto *w = panel->findChild<QDoubleSpinBox *>(QStringLiteral("t_blob_min_area"))) {
            QSignalBlocker b(w);
            w->setValue(m_params.value(QStringLiteral("minArea"), 100.0).toDouble());
        }
        break;
    case ToolboxBuiltin::EdgeDetection:
        if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("t_edge_filt"))) {
            QSignalBlocker g(cb);
            const int fs = qBound(3, m_params.value(QStringLiteral("filterSize"), 3).toInt() | 1, 39);
            const int idx = cb->findData(fs);
            if (idx >= 0) {
                cb->setCurrentIndex(idx);
            }
        }
        break;
    case ToolboxBuiltin::Distance: {
        const QString keys[] = {QStringLiteral("row1"), QStringLiteral("col1"), QStringLiteral("row2"),
                                QStringLiteral("col2")};
        const char *objs[] = {"t_dst_r1", "t_dst_c1", "t_dst_r2", "t_dst_c2"};
        for (int i = 0; i < 4; ++i) {
            if (auto *w = panel->findChild<QDoubleSpinBox *>(QLatin1String(objs[i]))) {
                QSignalBlocker b(w);
                w->setValue(m_params.value(keys[i]).toDouble());
            }
        }
        break;
    }
    case ToolboxBuiltin::WriteFile:
        if (auto *le = panel->findChild<QLineEdit *>(QStringLiteral("t_write_path"))) {
            QSignalBlocker b(le);
            le->setText(m_params.value(QStringLiteral("filePath"), QStringLiteral("output.png")).toString());
        }
        break;
    default:
        break;
    }
}
