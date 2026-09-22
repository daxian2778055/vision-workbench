#include "ColorConversionNode.h"
#include "Port.h"
#include "DataObject.h"
#include "Connection.h"
#include "FlowScene.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>
#include <QDebug>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTimer>
#include "AppLog.h"

using namespace HalconCpp;

ColorConversionNode::ColorConversionNode(QObject *parent)
    : HalconNode(parent)
{
    // 原 m_conversionType(RGB_TO_GRAY) 的默认值已移入 init()（参数表 = 唯一来源）
}

ColorConversionNode::~ColorConversionNode()
{}

void ColorConversionNode::init()
{
    setName(QStringLiteral("颜色转换"));
    m_type = IMAGE_PROCESSING;

    // 添加输入端口 - 图像数据（非图像获取算子必须有输入图像参数）
    addInputPort(QStringLiteral("输入图像"));

    // 添加输出端口 - 转换后的图像（一个红点代表所有输出参数）
    addOutputPort(QStringLiteral("输出图像"));

    // S1：参数默认值（原为构造函数里的成员初值 RGB_TO_GRAY）
    m_params[QStringLiteral("conversionType")] = static_cast<int>(RGB_TO_GRAY);

    // 初始化默认参数
    m_params["conversionType"] = static_cast<int>(RGB_TO_GRAY);
    m_params["moduleStatus"] = false; // 模块状态（bool量）
}

bool ColorConversionNode::process()
{
    try {
        // 检查输入数据
        VFP_DEBUG << "ColorConversionNode::process() called";
        
        // 尝试从参数中获取输入图像来源
        HImage inputImage;
        bool inputImageValid = false;
        
        // 首先检查是否有用户选择的输入图像来源
        if (m_params.contains("inputImageSource")) {
            QVariant sourceData = m_params["inputImageSource"];
            if (sourceData.canConvert<QPair<NodeBase*, int>>()) {
                QPair<NodeBase*, int> pair = sourceData.value<QPair<NodeBase*, int>>();
                NodeBase *sourceNode = pair.first;
                int portIndex = pair.second;
                
                VFP_DEBUG << "Using user selected input image source:" << sourceNode->name() << "port" << portIndex;
                
                QSharedPointer<DataObject> inputData = sourceNode->getOutputData(portIndex);
                if (inputData) {
                    inputImage = inputData->getHImage();
                    inputImageValid = inputImage.IsInitialized();
                    VFP_DEBUG << "Input image from user selection initialized:" << inputImageValid;
                    if (inputImageValid) {
                        HTuple width, height;
                        GetImageSize(inputImage, &width, &height);
                        VFP_DEBUG << "Input image size:" << width.I() << "x" << height.I();
                        HTuple channels;
                        CountChannels(inputImage, &channels);
                        VFP_DEBUG << "Input image channels:" << channels.I();
                    }
                }
            }
        }
        
        // 如果用户没有选择输入图像来源，使用默认的输入端口
        if (!inputImageValid) {
            VFP_DEBUG << "Using input port 0";
            VFP_DEBUG << "Input data size:" << m_inputData.size();
            VFP_DEBUG << "Input data contains 0:" << m_inputData.contains(0);
            if (m_inputData.contains(0)) {
                VFP_DEBUG << "Input data[0] is null:" << (m_inputData[0] == nullptr);
            }
            
            if (!m_inputData.contains(0) || !m_inputData[0]) {
                VFP_DEBUG << "No input image provided";
                return false;
            }

            // 获取输入图像
            QSharedPointer<DataObject> inputData = m_inputData[0];
            inputImage = inputData->getHImage();
            inputImageValid = inputImage.IsInitialized();
            VFP_DEBUG << "Input image from port initialized:" << inputImageValid;
            if (inputImageValid) {
                HTuple width, height;
                GetImageSize(inputImage, &width, &height);
                VFP_DEBUG << "Input image size:" << width.I() << "x" << height.I();
                HTuple channels;
                CountChannels(inputImage, &channels);
                VFP_DEBUG << "Input image channels:" << channels.I();
            }
        }
        
        if (!inputImageValid) {
            VFP_DEBUG << "Input image is not initialized";
            return false;
        }

        // 检查输入图像通道数
        HTuple channels;
        CountChannels(inputImage, &channels);
        int channelCount = channels.I();
        
        // 参数唯一来源：本轮取一次（局部快照）——避免逐次加锁，并保证同一轮用同一份转换类型
        const ConversionType conversionType =
            static_cast<ConversionType>(getParam(QStringLiteral("conversionType")).toInt());

        // 执行颜色转换
        HImage outputImage;
        switch (conversionType) {
        case RGB_TO_GRAY: {
            VFP_DEBUG << "Performing RGB to Gray conversion";
            if (channelCount == 1) {
                VFP_DEBUG << "Error: Input image is already grayscale, no need for conversion";
                return false;
            }
            cv::Mat src = OpencvUtil::himageToMat(inputImage);
            if (src.empty()) return false;
            cv::Mat gray;
            if (src.channels() == 3)
                cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
            else
                gray = src;
            outputImage = OpencvUtil::matToHimage(gray);
            VFP_DEBUG << "RGB to Gray conversion completed";
            break;
        }
        case RGB_TO_HSV:
        case RGB_TO_HSL: {
            // OpenCV 实现（替换版环境 HALCON TransFromRgb H 通道计算错误，实测 H=85 vs 期望 120）
            cv::Mat src = OpencvUtil::himageToMat(inputImage);
            if (src.empty()) return false;
            cv::Mat out;
            if (conversionType == RGB_TO_HSV)
                cv::cvtColor(src, out, cv::COLOR_BGR2HSV_FULL);
            else
                cv::cvtColor(src, out, cv::COLOR_BGR2HLS_FULL);
            outputImage = OpencvUtil::matToHimage(out);
            break;
        }
        case GRAY_TO_RGB: {
            VFP_DEBUG << "Performing Gray to RGB conversion";
            cv::Mat src = OpencvUtil::himageToMat(inputImage);
            if (src.empty()) return false;
            cv::Mat rgb;
            if (src.channels() == 1)
                cv::cvtColor(src, rgb, cv::COLOR_GRAY2BGR);
            else
                rgb = src;
            outputImage = OpencvUtil::matToHimage(rgb);
            break;
        }
        case HSV_TO_RGB:
        case HSL_TO_RGB: {
            // OpenCV 实现（与 TransFromRgb 同源缺陷，TransToRgb 一并改用 OpenCV）
            cv::Mat src = OpencvUtil::himageToMat(inputImage);
            if (src.empty()) return false;
            cv::Mat out;
            if (conversionType == HSV_TO_RGB)
                cv::cvtColor(src, out, cv::COLOR_HSV2BGR_FULL);
            else
                cv::cvtColor(src, out, cv::COLOR_HLS2BGR_FULL);
            outputImage = OpencvUtil::matToHimage(out);
            break;
        }
        default:
            VFP_DEBUG << "Unknown conversion type";
            return false;
        }

        // 检查输出图像
        VFP_DEBUG << "Output image initialized:" << outputImage.IsInitialized();
        if (outputImage.IsInitialized()) {
            HTuple width, height;
            GetImageSize(outputImage, &width, &height);
            VFP_DEBUG << "Output image size:" << width.I() << "x" << height.I();
            HTuple channels;
            CountChannels(outputImage, &channels);
            VFP_DEBUG << "Output image channels:" << channels.I();
        }

        // 存储输出图像
        m_outputImage = (HObject)outputImage;

        // 创建输出数据对象
        QSharedPointer<DataObject> outputData = QSharedPointer<DataObject>(new DataObject());
        outputData->setHImage(outputImage);

        // 输出数据到端口0（图像）
        m_outputData[0] = outputData;
        VFP_DEBUG << "Output data set to port 0";
        
        // 更新模块状态参数
        m_params["moduleStatus"] = true; // 执行成功
        VFP_DEBUG << "Module status set to true";
        
        // 发射图像转换完成信号
        emit imageConverted(outputImage);

        return true;
    }
    catch (HOperatorException &e) {
        VFP_DEBUG << "Halcon exception in ColorConversionNode::process():" << e.ErrorMessage().Text();
        // 设置模块状态为失败
        m_params["moduleStatus"] = false;
        return false;
    }
    catch (std::exception &e) {
        VFP_DEBUG << "Exception in ColorConversionNode::process():" << e.what();
        // 设置模块状态为失败
        m_params["moduleStatus"] = false;
        return false;
    }
    catch (...) {
        VFP_DEBUG << "Unknown exception in ColorConversionNode::process()";
        // 设置模块状态为失败
        m_params["moduleStatus"] = false;
        return false;
    }
}

QStringList ColorConversionNode::getAvailableConversions() const
{
    return {
        "RGB转灰度",
        "RGB转HSV",
        "RGB转HSL",
        "灰度转RGB",
        "HSV转RGB",
        "HSL转RGB"
    };
}

QWidget *ColorConversionNode::createParamPanel()
{
    try {
        QWidget *panel = new QWidget();
        QVBoxLayout *layout = new QVBoxLayout(panel);
        
        // 输入图像选择（非图像获取算子必须有输入图像参数）
        QLabel *inputImageLabel = new QLabel("输入图像:");
        QComboBox *inputImageCombo = new QComboBox();
        inputImageCombo->setObjectName("inputImageCombo");
        
        // 颜色转换类型选择
        QLabel *conversionTypeLabel = new QLabel("转换类型:");
        QComboBox *conversionTypeCombo = new QComboBox();
        conversionTypeCombo->addItems(getAvailableConversions());
        conversionTypeCombo->setCurrentIndex(getParam(QStringLiteral("conversionType")).toInt());
        
        // 连接信号
        connect(conversionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
            setParam(QStringLiteral("conversionType"), index);
        });
        
        // 连接输入图像选择信号
        connect(inputImageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [=](int index) {
            QVariant data = inputImageCombo->itemData(index);
            if (data.canConvert<QPair<NodeBase*, int>>()) {
                QPair<NodeBase*, int> pair = data.value<QPair<NodeBase*, int>>();
                NodeBase *sourceNode = pair.first;
                int portIndex = pair.second;
                
                // 这里可以保存选择的输入图像来源
                m_params["inputImageSource"] = QVariant::fromValue(QPair<NodeBase*, int>(sourceNode, portIndex));
                VFP_DEBUG << "Selected input image source:" << sourceNode->name() << "port" << portIndex;
            }
        });
        
        layout->addWidget(inputImageLabel);
        layout->addWidget(inputImageCombo);
        layout->addWidget(conversionTypeLabel);
        layout->addWidget(conversionTypeCombo);
        
        // 延迟初始化输入图像下拉菜单，确保能获取到场景
        QTimer::singleShot(100, [=]() {
            // 从参数中获取当前选择
            QVariant currentSelection;
            if (m_params.contains("inputImageSource")) {
                currentSelection = m_params["inputImageSource"];
            }
            
            // 清空现有选项
            inputImageCombo->clear();
            // 添加默认选项
            inputImageCombo->addItem("连接到输入端口", QVariant());
            
            // 尝试获取当前场景
            FlowScene *scene = nullptr;
            
            // 方法1: 通过节点的父对象查找
            QObject *parentObj = this->parent();
            while (parentObj) {
                scene = qobject_cast<FlowScene*>(parentObj);
                if (scene) {
                    break;
                }
                parentObj = parentObj->parent();
            }
            
            // 方法2: 如果方法1失败，尝试通过面板的父控件查找
            if (!scene) {
                QWidget *parentWidget = panel->parentWidget();
                while (parentWidget) {
                    scene = qobject_cast<FlowScene*>(parentWidget);
                    if (scene) {
                        break;
                    }
                    parentWidget = parentWidget->parentWidget();
                }
            }
            
            // 如果找到了场景，枚举所有可连接的输出参数
            if (scene) {
                VFP_DEBUG << "Found scene, nodes count:" << scene->nodes().size();
                
                // 检查是否有箭头连接到当前节点
                bool hasConnection = false;
                QList<MyProject::Connection*> connectionsList = scene->connections();
                for (int i = 0; i < connectionsList.size(); ++i) {
                    MyProject::Connection *connection = connectionsList[i];
                    if (connection->targetPort() && connection->targetPort()->node() == this) {
                        hasConnection = true;
                        break;
                    }
                }
                
                // 只有当有箭头连接时，才枚举其他节点的输出参数
                if (hasConnection) {
                    QList<NodeBase*> nodes = scene->nodes();
                    for (int i = 0; i < nodes.size(); ++i) {
                        NodeBase *node = nodes[i];
                        // 跳过当前节点
                        if (node == this) {
                            continue;
                        }
                        
                        VFP_DEBUG << "Checking node:" << node->name() << "hasExecuted:" << node->hasExecuted();
                        
                        // 检查节点是否已经执行过
                        if (node->hasExecuted()) {
                            // 枚举节点的所有输出端口
                            QList<Port*> outputPorts = node->outputPorts();
                            for (int j = 0; j < outputPorts.size(); ++j) {
                                Port *outputPort = outputPorts[j];
                                QSharedPointer<DataObject> outputData = node->getOutputData(j);
                                
                                VFP_DEBUG << "  Port:" << outputPort->name() << "outputData:" << (outputData ? "valid" : "null");
                                
                                // 检查输出数据是否存在
                                if (outputData) {
                                    // 将枚举类型转换为整数输出
                                    VFP_DEBUG << "  Data type:" << static_cast<int>(outputData->getType());
                                    // 检查输出数据类型是否为图像
                                    if (outputData->getType() == DataObject::DataType::Image) {
                                        QString itemText = QString("%1 [%2] - %3").arg(node->name()).arg(node->moduleId()).arg(outputPort->name());
                                        inputImageCombo->addItem(itemText, QVariant::fromValue(QPair<NodeBase*, int>(node, j)));
                                        VFP_DEBUG << "  Added item:" << itemText;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    VFP_DEBUG << "No connections to current node, skipping output parameter enumeration";
                }
            } else {
                VFP_DEBUG << "Scene not found";
            }
            
            // 从参数中恢复选择
            if (currentSelection.isValid() && currentSelection.canConvert<QPair<NodeBase*, int>>()) {
                QPair<NodeBase*, int> pair = currentSelection.value<QPair<NodeBase*, int>>();
                NodeBase *sourceNode = pair.first;
                int portIndex = pair.second;
                
                for (int i = 0; i < inputImageCombo->count(); ++i) {
                    QVariant data = inputImageCombo->itemData(i);
                    if (data.canConvert<QPair<NodeBase*, int>>()) {
                        QPair<NodeBase*, int> comboPair = data.value<QPair<NodeBase*, int>>();
                        if (comboPair.first == sourceNode && comboPair.second == portIndex) {
                            inputImageCombo->setCurrentIndex(i);
                            break;
                        }
                    }
                }
            }
        });
        
        return panel;
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in createParamPanel:" << e.what();
        return new QWidget();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in createParamPanel";
        return new QWidget();
    }
}

void ColorConversionNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    
    try {
        QComboBox *conversionTypeCombo = panel->findChild<QComboBox *>();
        if (conversionTypeCombo) {
            conversionTypeCombo->setCurrentIndex(getParam(QStringLiteral("conversionType")).toInt());
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in updateParamPanel:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in updateParamPanel";
    }
}

void ColorConversionNode::displayImage()
{
    try {
        // 显示输出图像
        QSharedPointer<DataObject> outputData = getOutputData(0);
        if (outputData) {
            HImage image = outputData->getHImage();
            if (image.IsInitialized()) {
                // 这里可以添加图像显示逻辑
                VFP_DEBUG << "Displaying image from ColorConversionNode";
            }
        }
    } catch (const std::exception &e) {
        VFP_DEBUG << "Exception in displayImage:" << e.what();
    } catch (...) {
        VFP_DEBUG << "Unknown exception in displayImage";
    }
}

bool ColorConversionNode::execute()
{
    return process();
}

QString ColorConversionNode::name() const
{
    return "ColorConversionNode";
}

NodeBase::NodeType ColorConversionNode::type() const
{
    return IMAGE_PROCESSING;
}

void ColorConversionNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类加锁 + 校验）。
    // 旧实现只处理 conversionType 且**不调基类** ⇒ 其它键的写入被静默丢弃（旁路，已纠正）。
    HalconNode::setParam(name, value);
}

// 说明：原 getParam 重写已**整段删除**——它对 conversionType 返回成员（绕过参数表的旁路）、
// 对其它键返回空 QVariant（**吞掉**基类结果）。删除后一律走基类 getParam（参数表 = 唯一来源）。

QJsonObject ColorConversionNode::toJson() const
{
    QJsonObject json = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值取自参数表（唯一来源）；executionSuccess 是执行结果，保持原样
    json["conversionType"] = QJsonValue::fromVariant(getParam(QStringLiteral("conversionType")));
    json["executionSuccess"] = m_executionSuccess;
    return json;
}

void ColorConversionNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // conversionType 由基类从 params 恢复（唯一来源）
    // 兼容更早方案：该键曾只存在顶层（无 params 段）。旧实现是 contains 守卫 + 保留成员原值
    // ⇒ 缺键**保留原值**，故保留 contains 守卫。
    if (!json.contains(QStringLiteral("params")) && json.contains("conversionType")) {
        setParam(QStringLiteral("conversionType"), json.value("conversionType").toVariant());
    }
    // 恢复执行状态
    if (json.contains("executionSuccess")) {
        m_executionSuccess = json["executionSuccess"].toBool();
    }
}