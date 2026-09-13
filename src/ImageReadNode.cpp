#include "ImageReadNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>
#include <QJsonObject>
#include <QVariant>
#include <QDir>
#include <QFileInfo>
#include <QSharedPointer>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QScrollArea>
#include <QMouseEvent>
#include <QFont>
#include <QTimer>
#include "AppLog.h"

ImageReadNode::ImageReadNode(QObject *parent)
    : HalconNode(parent)
{
    m_type = IMAGE_ACQUISITION;
    m_name = "Image Read";
    m_filePath = "";
    m_isDirectory = false;
    m_currentImageIndex = -1;
    m_displayImageIndex = -1;
    m_mono8Mode = false;
    m_autoSwitch = false;
    m_mode = Mode::SingleImage;
    m_imageWidth = 0;
    m_imageHeight = 0;
    m_pixelFormat = "";
    m_imageName = "";
    m_thumbnailNeedsUpdate = false;
}

ImageReadNode::~ImageReadNode()
{
}

void ImageReadNode::init()
{
    // ImageReadNode是图像获取算子，不需要输入图像参数
    // 添加输出端口 - 图像（一个红点代表所有输出参数）
    addOutputPort(QStringLiteral("输出图像"));
    
    // 初始化默认参数
    m_params["moduleStatus"] = false; // 模块状态（bool量）
}

void ImageReadNode::run(bool autoSwitch)
{
    VFP_DEBUG << "ImageReadNode::run() called";
    VFP_DEBUG << "File path:" << m_filePath;
    VFP_DEBUG << "Image files size:" << m_imageFiles.size();
    VFP_DEBUG << "Mono8 mode:" << m_mono8Mode;
    VFP_DEBUG << "Mode:" << static_cast<int>(m_mode);
    VFP_DEBUG << "Auto switch:" << m_autoSwitch;
    VFP_DEBUG << "Auto switch parameter:" << autoSwitch;
    VFP_DEBUG << "Current image index:" << m_currentImageIndex;
    
    if (!m_imageFiles.isEmpty()) {
        try {
            QString currentImagePath;
            int displayIndex = m_currentImageIndex;
            
            if (m_mode == Mode::MultiImage) {
                // Multi-image mode
                // 如果没有图像被选中，自动选中第一张图像
                if (m_currentImageIndex < 0 || m_currentImageIndex >= m_imageFiles.size()) {
                    // 执行算子时自动选中第一张图像
                    displayIndex = 0;
                    VFP_DEBUG << "Set display index to 0";
                } else {
                    displayIndex = m_currentImageIndex;
                }
                
                if (m_isDirectory) {
                    QDir dir(m_filePath);
                    currentImagePath = dir.absoluteFilePath(m_imageFiles[displayIndex]);
                } else {
                    currentImagePath = m_imageFiles[displayIndex];
                }
                
                // 更新显示索引，确保黄色框正确显示
                m_displayImageIndex = displayIndex;
                // 发出缩略图更新信号，确保黄色框显示
                m_thumbnailNeedsUpdate = true;
                emit thumbnailUpdated();
                
                // 执行完成后，如果启用了自动切换，切换到下一张图像，用于下一次执行
                if (m_autoSwitch && autoSwitch) {
                    int nextIndex = (m_currentImageIndex + 1) % m_imageFiles.size();
                    m_currentImageIndex = nextIndex;
                    VFP_DEBUG << "Auto switched to next image index:" << m_currentImageIndex;
                }
            } else {
                // Single image mode
                if (m_isDirectory && !m_imageFiles.isEmpty()) {
                    QDir dir(m_filePath);
                    currentImagePath = dir.absoluteFilePath(m_imageFiles[0]);
                } else {
                    currentImagePath = m_filePath;
                }
            }
            
            VFP_DEBUG << "Reading image:" << currentImagePath;
            ReadImage(&m_outputImage, currentImagePath.toStdString().c_str());
            VFP_DEBUG << "Image read successfully";
            
            // Check if image is initialized
            if (m_outputImage.IsInitialized()) {
                VFP_DEBUG << "Image is initialized";
                
                // Get image information
                HTuple width, height, type;
                GetImageSize(m_outputImage, &width, &height);
                GetImageType(m_outputImage, &type);
                m_imageWidth = width.I();
                m_imageHeight = height.I();
                
                // 转换像素格式表达
                HTuple channels;
                CountChannels(m_outputImage, &channels);
                if (channels.I() == 1) {
                    m_pixelFormat = "mono8";
                } else if (channels.I() == 3) {
                    m_pixelFormat = "RGB8";
                } else {
                    m_pixelFormat = type.S();
                }
                
                // Get image name
                QFileInfo fileInfo(currentImagePath);
                m_imageName = fileInfo.fileName();
                
                VFP_DEBUG << "Image width:" << m_imageWidth;
                VFP_DEBUG << "Image height:" << m_imageHeight;
                VFP_DEBUG << "Pixel format:" << m_pixelFormat;
                VFP_DEBUG << "Image name:" << m_imageName;
                
                // Apply mono8 mode if enabled
                if (m_mono8Mode) {
                    HTuple channels;
                    CountChannels(m_outputImage, &channels);
                    if (channels.I() > 1) {
                        VFP_DEBUG << "Converting color image to grayscale for mono8 mode";
                        cv::Mat src = OpencvUtil::himageToMat(HImage(m_outputImage));
                        if (!src.empty()) {
                            cv::Mat gray;
                            if (src.channels() == 3)
                                cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
                            else
                                gray = src;
                            m_outputImage = OpencvUtil::matToHimage(gray);
                        }
                        m_pixelFormat = "mono8";
                        VFP_DEBUG << "Image converted to grayscale";
                    } else {
                        // 如果已经是单通道图像，也设置为mono8
                        m_pixelFormat = "mono8";
                    }
                }
                
                // Set output data - 图像数据到端口0
                QSharedPointer<DataObject> outputData = QSharedPointer<DataObject>::create();
                outputData->setHImage(m_outputImage);
                setOutputData(0, outputData);
                
                // 更新模块状态参数 - 执行成功
                m_params["moduleStatus"] = true;
                m_executionSuccess = true;
                
                // Also emit a signal to show the image immediately
                VFP_DEBUG << "Emitting imageRead signal";
                emit imageRead(m_outputImage);
                // Emit signal to update thumbnails, ensure yellow frame shows correctly
                VFP_DEBUG << "Emitting thumbnailUpdated signal";
                emit thumbnailUpdated();
            } else {
                VFP_DEBUG << "Error: Image is not initialized after ReadImage";
                // 更新模块状态参数 - 执行失败
                m_params["moduleStatus"] = false;
                m_executionSuccess = false;
            }
        } catch (HException &e) {
            // Handle exception
            VFP_DEBUG << "Halcon exception:" << e.ErrorMessage().Text();
            // 更新模块状态参数 - 执行失败
            m_params["moduleStatus"] = false;
            m_executionSuccess = false;
        }
    } else {
        VFP_DEBUG << "No file path or image files";
        // 更新模块状态参数 - 执行失败
        m_params["moduleStatus"] = false;
        m_executionSuccess = false;
    }
}

void ImageReadNode::selectMultipleFiles()
{
    QStringList fileNames = QFileDialog::getOpenFileNames(nullptr, "Select Multiple Image Files", ".", "Image Files (*.bmp *.jpg *.jpeg *.png *.tif *.tiff)");
    if (!fileNames.isEmpty()) {
        m_imageFiles = fileNames;
        m_isDirectory = false;
        m_currentImageIndex = -1; // 初始化为 -1，表示没有图像被选中
        if (!m_imageFiles.isEmpty()) {
            m_filePath = m_imageFiles[0];
        }
    }
}

void ImageReadNode::updateImageFiles()
{
    m_imageFiles.clear();
    m_currentImageIndex = -1; // 初始化为 -1，表示没有图像被选中
    
    QDir dir(m_filePath);
    if (dir.exists()) {
        m_isDirectory = true;
        QStringList filters;
        filters << "*.bmp" << "*.jpg" << "*.jpeg" << "*.png" << "*.tif" << "*.tiff";
        m_imageFiles = dir.entryList(filters, QDir::Files, QDir::Name);
    } else {
        m_isDirectory = false;
        // Check if the file exists
        QFileInfo fileInfo(m_filePath);
        if (fileInfo.exists()) {
            m_imageFiles << m_filePath;
        }
    }
}

void ImageReadNode::setParam(const QString &name, const QVariant &value)
{
    HalconNode::setParam(name, value);
    if (name == "filePath") {
        m_filePath = value.toString();
        // 只有在单图模式或当前是目录模式时才更新图像文件列表
        // 避免在多图模式下选择多个文件后被清空
        if (m_mode == Mode::SingleImage || m_isDirectory) {
            updateImageFiles();
        }
        // 不再自动执行，需要用户点击"执行算子"按钮
    } else if (name == "mono8Mode") {
        m_mono8Mode = value.toBool();
    } else if (name == "autoSwitch") {
        m_autoSwitch = value.toBool();
    } else if (name == "mode") {
        m_mode = static_cast<Mode>(value.toInt());
        // 当切换到单图模式时，更新图像文件列表
        if (m_mode == Mode::SingleImage) {
            updateImageFiles();
        }
    }
}

QVariant ImageReadNode::getParam(const QString &name) const
{
    if (name == "filePath") {
        return m_filePath;
    } else if (name == "mono8Mode") {
        return m_mono8Mode;
    } else if (name == "autoSwitch") {
        return m_autoSwitch;
    } else if (name == "mode") {
        return static_cast<int>(m_mode);
    } else if (name == "imageWidth") {
        return m_imageWidth;
    } else if (name == "imageHeight") {
        return m_imageHeight;
    } else if (name == "pixelFormat") {
        return m_pixelFormat;
    } else if (name == "imageName") {
        return m_imageName;
    } else if (name == "imagePath") {
        return m_filePath;
    }
    return HalconNode::getParam(name);
}

QJsonObject ImageReadNode::toJson() const
{
    QJsonObject json = HalconNode::toJson();
    json["filePath"] = m_filePath;
    json["mono8Mode"] = m_mono8Mode;
    json["autoSwitch"] = m_autoSwitch;
    json["mode"] = static_cast<int>(m_mode);
    json["imageWidth"] = m_imageWidth;
    json["imageHeight"] = m_imageHeight;
    json["pixelFormat"] = m_pixelFormat;
    json["imageName"] = m_imageName;
    json["executionSuccess"] = m_executionSuccess;
    return json;
}

void ImageReadNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    m_filePath = json["filePath"].toString();
    if (json.contains("mono8Mode")) {
        m_mono8Mode = json["mono8Mode"].toBool();
    }
    if (json.contains("autoSwitch")) {
        m_autoSwitch = json["autoSwitch"].toBool();
    }
    if (json.contains("mode")) {
        m_mode = static_cast<Mode>(json["mode"].toInt());
    }
    if (json.contains("imageWidth")) {
        m_imageWidth = json["imageWidth"].toInt();
    }
    if (json.contains("imageHeight")) {
        m_imageHeight = json["imageHeight"].toInt();
    }
    if (json.contains("pixelFormat")) {
        m_pixelFormat = json["pixelFormat"].toString();
    }
    if (json.contains("imageName")) {
        m_imageName = json["imageName"].toString();
    }
    // 恢复执行状态
    if (json.contains("executionSuccess")) {
        m_executionSuccess = json["executionSuccess"].toBool();
    }
    
    // 更新图像文件列表
    updateImageFiles();
}

bool ImageReadNode::process() {
    // 确保在执行前如果没有选择图像，自动选择第一张
    if (m_currentImageIndex == -1 && !m_imageFiles.isEmpty()) {
        m_currentImageIndex = 0;
        m_displayImageIndex = 0;
        // 设置标志而不是直接发射信号，因为参数面板可能还没有创建
        m_thumbnailNeedsUpdate = true;
        emit thumbnailUpdated();
    }
    
    run(true); // 自动切换到下一张图像
    return m_executionSuccess;
}

QWidget *ImageReadNode::createParamPanel()
{
    QWidget *panel = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(panel);
    
    // 添加模式选择
    QLabel *modeLabel = new QLabel("模式:");
    layout->addWidget(modeLabel);
    
    QHBoxLayout *modeLayout = new QHBoxLayout();
    QRadioButton *singleImageRadio = new QRadioButton("单图模式");
    QRadioButton *multiImageRadio = new QRadioButton("多图模式");
    modeLayout->addWidget(singleImageRadio);
    modeLayout->addWidget(multiImageRadio);
    layout->addLayout(modeLayout);
    
    // 设置默认模式
    if (m_mode == Mode::SingleImage) {
        singleImageRadio->setChecked(true);
    } else {
        multiImageRadio->setChecked(true);
    }
    
    // 添加文件路径选择
    QLabel *filePathLabel = new QLabel("文件路径:");
    layout->addWidget(filePathLabel);
    
    QHBoxLayout *filePathLayout = new QHBoxLayout();
    QLineEdit *filePathEdit = new QLineEdit(m_filePath);
    filePathEdit->setObjectName("filePathEdit");
    filePathLayout->addWidget(filePathEdit);
    
    QPushButton *browseButton = new QPushButton("浏览文件");
    filePathLayout->addWidget(browseButton);
    
    QPushButton *browseDirButton = new QPushButton("浏览文件夹");
    filePathLayout->addWidget(browseDirButton);
    
    QPushButton *browseMultiButton = new QPushButton("选择多文件");
    filePathLayout->addWidget(browseMultiButton);
    layout->addLayout(filePathLayout);
    
    // 添加自动切换开关（仅在多图模式下显示）
    QHBoxLayout *autoSwitchLayout = new QHBoxLayout();
    QLabel *autoSwitchLabel = new QLabel("自动切换:");
    QCheckBox *autoSwitchCheckBox = new QCheckBox();
    autoSwitchCheckBox->setChecked(m_autoSwitch);
    autoSwitchLayout->addWidget(autoSwitchLabel);
    autoSwitchLayout->addWidget(autoSwitchCheckBox);
    autoSwitchLayout->addStretch();
    layout->addLayout(autoSwitchLayout);
    
    // 添加mono8模式复选框
    QCheckBox *mono8CheckBox = new QCheckBox("Mono8 Mode");
    mono8CheckBox->setChecked(m_mono8Mode);
    layout->addWidget(mono8CheckBox);
    
    // 添加图像缩略图显示（仅在多图模式下显示）
    QLabel *thumbnailLabel = new QLabel("图像缩略图:");
    layout->addWidget(thumbnailLabel);
    
    QWidget *thumbnailWidget = new QWidget();
    QHBoxLayout *thumbnailLayout = new QHBoxLayout(thumbnailWidget);
    thumbnailLayout->setContentsMargins(0, 0, 0, 0);
    
    // 添加滚动区域
    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(thumbnailWidget);
    layout->addWidget(scrollArea);
    

    
    // 连接信号
    connect(singleImageRadio, &QRadioButton::toggled, [this, autoSwitchLayout, thumbnailLabel, scrollArea](bool checked) {
        if (checked) {
            setParam("mode", static_cast<int>(Mode::SingleImage));
            autoSwitchLayout->setEnabled(false);
            thumbnailLabel->setEnabled(false);
            scrollArea->setEnabled(false);
        }
    });
    
    connect(multiImageRadio, &QRadioButton::toggled, [this, autoSwitchLayout, thumbnailLabel, scrollArea, thumbnailLayout](bool checked) {
        if (checked) {
            setParam("mode", static_cast<int>(Mode::MultiImage));
            autoSwitchLayout->setEnabled(true);
            thumbnailLabel->setEnabled(true);
            scrollArea->setEnabled(true);
            updateThumbnails(thumbnailLayout);
        }
    });
    
    // 连接浏览按钮信号
    connect(browseButton, &QPushButton::clicked, [this, filePathEdit, thumbnailLayout]() {
        QString fileName = QFileDialog::getOpenFileName(nullptr, "Select Image File", ".", "Image Files (*.bmp *.jpg *.jpeg *.png *.tif *.tiff)");
        if (!fileName.isEmpty()) {
            filePathEdit->setText(fileName);
            setParam("filePath", fileName);
            updateThumbnails(thumbnailLayout);
        }
    });
    
    connect(browseDirButton, &QPushButton::clicked, [this, filePathEdit, thumbnailLayout]() {
        QString dirName = QFileDialog::getExistingDirectory(nullptr, "Select Directory", ".");
        if (!dirName.isEmpty()) {
            m_currentImageIndex = -1; // 重置为 -1，表示没有图像被选中
            m_displayImageIndex = -1; // 重置显示索引
            m_isDirectory = true;
            filePathEdit->setText(dirName);
            setParam("filePath", dirName);
            updateImageFiles();
            updateThumbnails(thumbnailLayout);
        }
    });
    
    connect(browseMultiButton, &QPushButton::clicked, [this, filePathEdit, thumbnailLayout]() {
        m_currentImageIndex = -1; // 重置为 -1，表示没有图像被选中
        m_displayImageIndex = -1; // 重置显示索引
        selectMultipleFiles();
        if (!m_filePath.isEmpty()) {
            filePathEdit->setText(m_filePath);
        }
        m_isDirectory = false;
        updateImageFiles();
        updateThumbnails(thumbnailLayout);
    });
    
    // 连接文本编辑信号
    connect(filePathEdit, &QLineEdit::textChanged, [this, thumbnailLayout](const QString &text) {
        setParam("filePath", text);
        updateThumbnails(thumbnailLayout);
    });
    
    // 连接自动切换信号
    connect(autoSwitchCheckBox, &QCheckBox::toggled, [this](bool checked) {
        setParam("autoSwitch", checked);
    });
    
    // 连接mono8模式复选框信号
    connect(mono8CheckBox, &QCheckBox::toggled, [this](bool checked) {
        setParam("mono8Mode", checked);
    });
    
    // 初始更新缩略图
    if (m_mode == Mode::MultiImage) {
        updateThumbnails(thumbnailLayout);
    } else {
        autoSwitchLayout->setEnabled(false);
        thumbnailLabel->setEnabled(false);
        scrollArea->setEnabled(false);
    }
    
    // 连接缩略图更新信号
    connect(this, &ImageReadNode::thumbnailUpdated, [this, thumbnailLayout]() {
        // 延迟更新缩略图，避免在事件处理中修改UI
        QTimer::singleShot(0, [this, thumbnailLayout]() {
            updateThumbnails(thumbnailLayout);
        });
    });
    
    // 连接参数面板显示信号，确保参数面板显示时能正确更新缩略图
    connect(this, &ImageReadNode::thumbnailUpdated, [this, thumbnailLayout]() {
        // 确保在参数面板显示时能正确更新缩略图
        if (thumbnailLayout) {
            QTimer::singleShot(0, [this, thumbnailLayout]() {
                updateThumbnails(thumbnailLayout);
            });
        }
    });
    
    // 检查是否需要立即更新缩略图（例如，在参数面板创建之前就开始执行）
    if (m_thumbnailNeedsUpdate) {
        m_thumbnailNeedsUpdate = false;
        // 立即更新缩略图，不需要延迟
        updateThumbnails(thumbnailLayout);
    }
    
    return panel;
}

void ImageReadNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    
    QLineEdit *filePathEdit = panel->findChild<QLineEdit*>("filePathEdit");
    if (filePathEdit) {
        filePathEdit->setText(m_filePath);
    }
    
    // 查找并更新所有复选框
    QList<QCheckBox*> checkBoxes = panel->findChildren<QCheckBox*>();
    for (QCheckBox *checkBox : checkBoxes) {
        // 检查复选框的文本或属性来确定它是哪个复选框
        if (checkBox->text() == "Mono8 Mode") {
            checkBox->setChecked(m_mono8Mode);
        }
        // 不再假设其他复选框是自动切换，避免覆盖用户的设置
    }
    
    // 查找并更新模式选择单选按钮
    QList<QRadioButton*> radioButtons = panel->findChildren<QRadioButton*>();
    for (QRadioButton *radio : radioButtons) {
        if (radio->text() == "单图模式") {
            radio->setChecked(m_mode == Mode::SingleImage);
        } else if (radio->text() == "多图模式") {
            radio->setChecked(m_mode == Mode::MultiImage);
        }
    }
    
    // 更新输出参数
    QLineEdit *widthEdit = panel->findChild<QLineEdit*>("widthEdit");
    if (widthEdit) {
        widthEdit->setText(QString::number(m_imageWidth));
    }
    
    QLineEdit *heightEdit = panel->findChild<QLineEdit*>("heightEdit");
    if (heightEdit) {
        heightEdit->setText(QString::number(m_imageHeight));
    }
    
    QLineEdit *formatEdit = panel->findChild<QLineEdit*>("formatEdit");
    if (formatEdit) {
        formatEdit->setText(m_pixelFormat);
    }
    
    QLineEdit *pathEdit = panel->findChild<QLineEdit*>("pathEdit");
    if (pathEdit) {
        pathEdit->setText(m_filePath);
        pathEdit->setToolTip(m_filePath);
    }
    
    QLineEdit *nameEdit = panel->findChild<QLineEdit*>("nameEdit");
    if (nameEdit) {
        nameEdit->setText(m_imageName);
    }
}

void ImageReadNode::displayImage()
{
    QSharedPointer<DataObject> outputData = getOutputData(0);
    if (outputData) {
        HalconCpp::HImage image = outputData->getHImage();
        if (image.IsInitialized()) {
            // 这里可以添加图像显示逻辑
            VFP_DEBUG << "Displaying image from ImageReadNode";
        }
    }
}

void ImageReadNode::updateThumbnails(QHBoxLayout *thumbnailLayout)
{
    // 清空现有缩略图
    QLayoutItem *item;
    while ((item = thumbnailLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    
    // 添加新缩略图
    for (int i = 0; i < m_imageFiles.size(); ++i) {
        QString imagePath;
        if (m_isDirectory) {
            QDir dir(m_filePath);
            imagePath = dir.absoluteFilePath(m_imageFiles[i]);
        } else {
            imagePath = m_imageFiles[i];
        }
        
        QWidget *thumbnailItem = new QWidget();
        QVBoxLayout *itemLayout = new QVBoxLayout(thumbnailItem);
        itemLayout->setContentsMargins(5, 5, 5, 5);
        
        QPushButton *imageButton = new QPushButton();
        QPixmap pixmap(imagePath);
        if (!pixmap.isNull()) {
            pixmap = pixmap.scaled(80, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            imageButton->setIcon(QIcon(pixmap));
            imageButton->setIconSize(QSize(80, 60));
        }
        imageButton->setFixedSize(80, 60);
        imageButton->setStyleSheet("QPushButton { border: none; background: transparent; }");
        
        // 添加选中边框
        if (m_displayImageIndex >= 0 && i == m_displayImageIndex) {
            imageButton->setStyleSheet("QPushButton { border: 2px solid yellow; background-color: #f0f0f0; }");
        }
        
        QLabel *nameLabel = new QLabel(QFileInfo(imagePath).fileName());
        nameLabel->setAlignment(Qt::AlignCenter);
        nameLabel->setWordWrap(true);
        nameLabel->setFixedWidth(80);
        nameLabel->setFont(QFont("Arial", 8));
        
        itemLayout->addWidget(imageButton);
        itemLayout->addWidget(nameLabel);
        
        // 点击切换图像
        connect(imageButton, &QPushButton::clicked, [this, i, thumbnailLayout]() {
            // 先更新当前图像索引和显示索引
            m_currentImageIndex = i;
            m_displayImageIndex = i;
            // 立即更新缩略图，让黄色选中框移动到新选中的图像上
            updateThumbnails(thumbnailLayout);
            // 然后执行算子，显示选中的图像
            run(false); // 不自动切换到下一张图像
        });
        
        thumbnailLayout->addWidget(thumbnailItem);
    }
}
