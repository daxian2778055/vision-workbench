# VisionFlowPlatform API 文档

## 概述

本文档描述了 VisionFlowPlatform 项目的核心 API，包括基类、接口、数据结构和工具类。

## 目录

1. [核心基类](#核心基类)
2. [节点接口](#节点接口)
3. [数据类型](#数据类型)
4. [端口系统](#端口系统)
5. [流程管理](#流程管理)
6. [相机接口](#相机接口)
7. [通信接口](#通信接口)
8. [工具类](#工具类)

---

## 核心基类

### NodeBase

所有算子节点的抽象基类。

```cpp
class NodeBase : public QObject {
    Q_OBJECT
    
public:
    enum NodeType {
        IMAGE_ACQUISITION,  // 图像采集
        IMAGE_PROCESSING,   // 图像处理
        SHAPE_ANALYSIS,     // 形状分析
        MEASUREMENT,        // 测量
        LOGIC,              // 逻辑控制
        OUTPUT              // 输出
    };
    
    // 构造/析构
    explicit NodeBase(QObject *parent = nullptr);
    virtual ~NodeBase();
    
    // 纯虚函数 - 必须实现
    virtual void init() = 0;
    virtual void run(bool autoSwitch = true) = 0;
    virtual void setParam(const QString &name, const QVariant &value) = 0;
    virtual QVariant getParam(const QString &name) const = 0;
    virtual void drawResult() = 0;
    virtual QJsonObject toJson() const = 0;
    virtual void fromJson(const QJsonObject &json) = 0;
    
    // 可选虚函数
    virtual QWidget *createParamPanel();
    virtual void updateParamPanel(QWidget *panel);
    virtual void displayImage();
    virtual QList<QString> getAllParamNames() const;
    
    // 端口管理
    void addInputPort(const QString &name, PortDataType type);
    void addOutputPort(const QString &name, PortDataType type);
    QList<Port*> inputPorts() const;
    QList<Port*> outputPorts() const;
    
    // 数据获取
    HObject getInputImage() const;
    void setOutputImage(const HObject &image);
    HObject getOutputImage() const;
    
    // 状态管理
    bool isRunning() const;
    void setRunning(bool running);
    QString errorMessage() const;
    void setError(const QString &message);
    
    // 场景引用
    FlowScene *flowScene() const;
    void setFlowScene(FlowScene *scene);
    
signals:
    void executionStarted();
    void executionFinished();
    void executionError(const QString &error);
    void imageReady(const HObject &image);
    void paramChanged(const QString &name, const QVariant &value);
};
```

**使用示例**：
```cpp
class MyNode : public NodeBase {
    Q_OBJECT
    
public:
    MyNode(QObject *parent = nullptr) : NodeBase(parent) {}
    
    void init() override {
        addInputPort("图像", PortDataType::Image);
        addOutputPort("结果", PortDataType::Image);
        m_params["threshold"] = 128;
    }
    
    void run(bool autoSwitch = true) override {
        HObject input = getInputImage();
        HObject output;
        threshold(input, &output, m_params["threshold"].toInt(), 255);
        setOutputImage(output);
    }
    
    // ... 其他虚函数实现
};
```

### HalconNode

HALCON 算子的中间基类。

```cpp
class HalconNode : public NodeBase {
    Q_OBJECT
    
public:
    explicit HalconNode(QObject *parent = nullptr);
    virtual ~HalconNode();
    
    // HALCON 特定方法
    HalconWindow *getHalconWindow() const;
    void setHalconWindow(HalconWindow *window);
    
    // 图像处理辅助方法
    HObject preprocessImage(const HObject &input);
    HObject postprocessImage(const HObject &input);
    
    // 结果绘制
    void drawRegion(const HRegion &region, const QString &color = "green");
    void drawContour(const HXLD &contour, const QString &color = "red");
    void drawText(const QString &text, int row, int col, const QString &color = "white");
};
```

---

## 节点接口

### 图像采集节点

#### MvsImageSourceNode

海康 MVS 相机图像源节点。

```cpp
class MvsImageSourceNode : public NodeBase {
    Q_OBJECT
    
public:
    // 相机控制
    bool openCamera();
    void closeCamera();
    bool isOpen() const;
    
    // 图像采集
    bool startCapture();
    void stopCapture();
    HObject grabImage();
    
    // 参数设置
    void setExposure(double ms);
    void setGain(double db);
    void setTriggerMode(TriggerMode mode);
    void setPixelFormat(const QString &format);
    
    // 设备信息
    QString deviceName() const;
    QString serialNumber() const;
    
signals:
    void imageGrabbed(const HObject &image);
    void cameraOpened();
    void cameraClosed();
    void errorOccurred(const QString &error);
};
```

#### HalconImageSourceNode

HALCON 图像源节点。

```cpp
class HalconImageSourceNode : public NodeBase {
    Q_OBJECT
    
public:
    // 文件操作
    bool loadImage(const QString &path);
    bool loadDirectory(const QString &path);
    bool loadSequence(const QString &pattern, int start, int end);
    
    // 图像获取
    HObject currentImage() const;
    bool hasNext() const;
    HObject nextImage();
    
    // 参数设置
    void setFilePath(const QString &path);
    void setDirectory(const QString &path);
    void setFilePattern(const QString &pattern);
};
```

### 图像处理节点

#### ThresholdNode

阈值分割节点。

```cpp
class ThresholdNode : public HalconNode {
    Q_OBJECT
    
public:
    // 参数
    void setThresholdMin(int value);
    void setThresholdMax(int value);
    int thresholdMin() const;
    int thresholdMax() const;
    
    // 处理
    bool process() override;
    HObject resultRegion() const;
};
```

#### BlurNode

图像模糊节点。

```cpp
class BlurNode : public HalconNode {
    Q_OBJECT
    
public:
    // 参数
    void setKernelSize(int size);
    void setSigma(double sigma);
    int kernelSize() const;
    double sigma() const;
    
    // 处理
    bool process() override;
    HObject filteredImage() const;
};
```

#### MorphologyNode

形态学操作节点。

```cpp
class MorphologyNode : public HalconNode {
    Q_OBJECT
    
public:
    enum MorphType {
        EROSION,
        DILATION,
        OPENING,
        CLOSING,
        TOP_HAT,
        BOTTOM_HAT
    };
    
    // 参数
    void setMorphType(MorphType type);
    void setKernelSize(int size);
    void setIterations(int iterations);
    
    // 处理
    bool process() override;
    HObject resultRegion() const;
};
```

### 分析节点

#### BlobAnalysisNode

Blob 分析节点。

```cpp
class BlobAnalysisNode : public HalconNode {
    Q_OBJECT
    
public:
    // 参数
    void setAreaMin(double min);
    void setAreaMax(double max);
    void setCircularityMin(double min);
    void setCircularityMax(double max);
    
    // 结果
    int blobCount() const;
    QVector<double> areas() const;
    QVector<QPointF> centers() const;
    QVector<QRectF> boundingBoxes() const;
    
    // 处理
    bool process() override;
    HObject selectedRegions() const;
};
```

#### EdgeDetectionNode

边缘检测节点。

```cpp
class EdgeDetectionNode : public HalconNode {
    Q_OBJECT
    
public:
    enum EdgeMethod {
        CANNY,
        SOBEL,
        LAPLACIAN,
        PREWITT
    };
    
    // 参数
    void setMethod(EdgeMethod method);
    void setLowThreshold(double threshold);
    void setHighThreshold(double threshold);
    void setKernelSize(int size);
    
    // 处理
    bool process() override;
    HObject edgeImage() const;
    HXLD edgeContours() const;
};
```

### 测量节点

#### FindLineNode

直线查找节点。

```cpp
class FindLineNode : public HalconNode {
    Q_OBJECT
    
public:
    // 参数
    void setRoi(const QRectF &roi);
    void setEdgeThreshold(double threshold);
    void setEdgePolarity(const QString &polarity);
    
    // 结果
    QLineF line() const;
    double angle() const;
    QPointF startPoint() const;
    QPointF endPoint() const;
    
    // 处理
    bool process() override;
};
```

#### FindCircleNode

圆查找节点。

```cpp
class FindCircleNode : public HalconNode {
    Q_OBJECT
    
public:
    // 参数
    void setRoi(const QRectF &roi);
    void setEdgeThreshold(double threshold);
    void setRadiusMin(double min);
    void setRadiusMax(double max);
    
    // 结果
    QPointF center() const;
    double radius() const;
    double startAngle() const;
    double endAngle() const;
    
    // 处理
    bool process() override;
};
```

---

## 数据类型

### DataObject

端口间传递的数据包。

```cpp
class DataObject {
public:
    enum DataType {
        Image,      // HObject 图像
        Region,     // HRegion 区域
        XLD,        // HXLD 轮廓
        Number,     // double 数值
        String,     // QString 字符串
        Point,      // QPointF 点
        Line,       // QLineF 直线
        Circle,     // 圆 (中心+半径)
        Matrix,     // HTuple 矩阵
        Unknown     // 未知类型
    };
    
    // 构造
    DataObject();
    DataObject(DataType type, const QVariant &value);
    
    // 类型检查
    DataType type() const;
    bool isImage() const;
    bool isRegion() const;
    bool isNumber() const;
    
    // 数据获取
    HObject image() const;
    HRegion region() const;
    HXLD xld() const;
    double number() const;
    QString string() const;
    QPointF point() const;
    QLineF line() const;
    
    // 数据设置
    void setImage(const HObject &image);
    void setRegion(const HRegion &region);
    void setNumber(double value);
    void setString(const QString &value);
    
    // 序列化
    QJsonObject toJson() const;
    static DataObject fromJson(const QJsonObject &json);
};
```

### PortDataType

端口数据类型枚举。

```cpp
enum class PortDataType {
    Image,      // 图像数据
    Region,     // 区域数据
    XLD,        // 轮廓数据
    Number,     // 数值数据
    String,     // 字符串数据
    Point,      // 点数据
    Line,       // 直线数据
    Circle,     // 圆数据
    Matrix,     // 矩阵数据
    Any         // 任意类型
};
```

---

## 端口系统

### Port

端口数据模型。

```cpp
class Port : public QObject {
    Q_OBJECT
    
public:
    enum PortDirection {
        Input,
        Output
    };
    
    // 构造
    Port(PortDirection direction, const QString &name, 
         PortDataType type, NodeBase *parent);
    
    // 属性
    PortDirection direction() const;
    QString name() const;
    PortDataType dataType() const;
    NodeBase *node() const;
    
    // 连接管理
    bool connectTo(Port *other);
    void disconnectFrom(Port *other);
    void disconnectAll();
    
    // 连接查询
    QList<Port*> connectedPorts() const;
    bool isConnected() const;
    int connectionCount() const;
    
    // 数据访问
    QSharedPointer<DataObject> data() const;
    void setData(QSharedPointer<DataObject> data);
    
signals:
    void connected(Port *other);
    void disconnected(Port *other);
    void dataChanged();
};
```

### PortGraphicsItem

端口图形项。

```cpp
class PortGraphicsItem : public QGraphicsEllipseItem {
public:
    // 构造
    PortGraphicsItem(Port *port, QGraphicsItem *parent = nullptr);
    
    // 属性
    Port *port() const;
    QPointF connectionPoint() const;
    
    // 外观
    void setColor(const QColor &color);
    void setHighlight(bool highlight);
    
    // 交互
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
};
```

---

## 流程管理

### FlowScene

流程画布场景。

```cpp
class FlowScene : public QGraphicsScene {
    Q_OBJECT
    
public:
    explicit FlowScene(QObject *parent = nullptr);
    ~FlowScene();
    
    // 节点管理
    void addNode(NodeBase *node);
    void removeNode(NodeBase *node);
    QList<NodeBase*> nodes() const;
    NodeBase *nodeAt(const QPointF &pos) const;
    
    // 连线管理
    Connection *createConnection(Port *source, Port *target);
    void removeConnection(Connection *connection);
    QList<Connection*> connections() const;
    
    // 选择
    QList<NodeBase*> selectedNodes() const;
    void clearSelection();
    
    // 编辑锁定
    void setEditLocked(bool locked);
    bool isEditLocked() const;
    
    // 序列化
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);
    
    // 清空
    void clear();
    
signals:
    void nodeAdded(NodeBase *node);
    void nodeRemoved(NodeBase *node);
    void connectionCreated(Connection *connection);
    void connectionRemoved(Connection *connection);
    void selectionChanged();
    void editLockChanged(bool locked);
};
```

### FlowExecutor

流程执行器。

```cpp
class FlowExecutor : public QThread {
    Q_OBJECT
    
public:
    enum FlowMode {
        Continuous,         // 连续模式
        SoftwareTrigger,    // 软触发模式
        HardwareTrigger     // 硬触发模式
    };
    
    enum ExecutionState {
        Idle,               // 空闲
        Running,            // 运行中
        Paused,             // 已暂停
        Stopped,            // 已停止
        Error               // 错误
    };
    
    explicit FlowExecutor(QObject *parent = nullptr);
    ~FlowExecutor();
    
    // 场景设置
    void setScene(FlowScene *scene);
    FlowScene *scene() const;
    
    // 流程控制
    void startExecution();
    void stopExecution();
    void pauseExecution();
    void resumeExecution();
    
    // 模式设置
    void setFlowMode(FlowMode mode);
    FlowMode flowMode() const;
    
    // 状态查询
    ExecutionState executionState() const;
    bool isRunning() const;
    bool isPaused() const;
    
    // 执行信息
    int currentNodeIndex() const;
    int totalNodes() const;
    QString currentNodeName() const;
    
signals:
    void executionStarted();
    void executionStopped();
    void executionPaused();
    void executionResumed();
    void executionFinished();
    void executionError(const QString &error);
    void nodeExecuted(NodeBase *node);
    void progressChanged(int current, int total);
    
protected:
    void run() override;
    
private:
    // 拓扑排序
    QList<NodeBase*> topologicalSort() const;
    
    // 数据传播
    void propagateData(NodeBase *node);
    
    // 循环执行
    void executeContinuous();
    void executeOnce();
};
```

---

## 相机接口

### GlobalCameraManager

全局相机管理器。

```cpp
class GlobalCameraManager : public QObject {
    Q_OBJECT
    
public:
    static GlobalCameraManager &instance();
    
    // 设备管理
    bool addCamera(const QString &name, MvsImageSourceNode *camera);
    void removeCamera(const QString &name);
    MvsImageSourceNode *camera(const QString &name) const;
    QStringList cameraNames() const;
    
    // 全局操作
    bool openAll();
    void closeAll();
    bool startCaptureAll();
    void stopCaptureAll();
    
    // 配置
    void setDefaultExposure(double ms);
    void setDefaultGain(double db);
    void setDefaultTriggerMode(TriggerMode mode);
    
signals:
    void cameraAdded(const QString &name);
    void cameraRemoved(const QString &name);
    void cameraOpened(const QString &name);
    void cameraClosed(const QString &name);
    void errorOccurred(const QString &name, const QString &error);
};
```

---

## 通信接口

### CommunicationManager

通信管理器。

```cpp
class CommunicationManager : public QObject {
    Q_OBJECT
    
public:
    static CommunicationManager &instance();
    
    // 设备管理
    bool addDevice(const QString &name, const QString &type, 
                   const QVariantMap &config);
    void removeDevice(const QString &name);
    CommunicationNodeBase *device(const QString &name) const;
    QStringList deviceNames() const;
    
    // 连接管理
    bool openDevice(const QString &name);
    void closeDevice(const QString &name);
    bool isDeviceOpen(const QString &name) const;
    
    // 数据发送
    bool sendData(const QString &deviceName, const QByteArray &data);
    bool sendData(const QString &deviceName, const QString &data);
    
    // 事件管理
    void addReceiveEvent(const QString &deviceName, ReceiveEvent *event);
    void addSendEvent(const QString &deviceName, SendEvent *event);
    
signals:
    void deviceAdded(const QString &name);
    void deviceRemoved(const QString &name);
    void deviceOpened(const QString &name);
    void deviceClosed(const QString &name);
    void dataReceived(const QString &deviceName, const QByteArray &data);
    void errorOccurred(const QString &deviceName, const QString &error);
};
```

### ReceiveEvent

接收事件基类。

```cpp
class ReceiveEvent : public QObject {
    Q_OBJECT
    
public:
    explicit ReceiveEvent(const QString &name, QObject *parent = nullptr);
    virtual ~ReceiveEvent();
    
    // 属性
    QString name() const;
    void setName(const QString &name);
    
    // 数据处理
    virtual void processData(const QByteArray &data) = 0;
    
signals:
    void eventGenerated(const QString &eventId, const QVariantMap &fields);
    void errorOccurred(const QString &error);
};
```

### SendEvent

发送事件基类。

```cpp
class SendEvent : public QObject {
    Q_OBJECT
    
public:
    explicit SendEvent(const QString &name, QObject *parent = nullptr);
    virtual ~SendEvent();
    
    // 属性
    QString name() const;
    void setName(const QString &name);
    
    // 数据生成
    virtual QByteArray generateData() = 0;
    
    // 触发
    void trigger();
    
signals:
    void dataReady(const QByteArray &data);
    void errorOccurred(const QString &error);
};
```

---

## 工具类

### ProjectManager

项目管理器。

```cpp
class ProjectManager : public QObject {
    Q_OBJECT
    
public:
    static ProjectManager &instance();
    
    // 项目操作
    bool createProject(const QString &path);
    bool openProject(const QString &path);
    bool saveProject();
    bool saveProjectAs(const QString &path);
    void closeProject();
    
    // 项目信息
    QString projectPath() const;
    QString projectName() const;
    bool isModified() const;
    bool hasUnsavedChanges() const;
    
    // 流程管理
    bool addFlow(const QString &name);
    bool removeFlow(const QString &name);
    FlowScene *flowScene(const QString &name) const;
    QStringList flowNames() const;
    
signals:
    void projectCreated(const QString &path);
    void projectOpened(const QString &path);
    void projectSaved(const QString &path);
    void projectClosed();
    void flowAdded(const QString &name);
    void flowRemoved(const QString &name);
    void modificationChanged(bool modified);
};
```

### GlobalVariableManager

全局变量管理器。

```cpp
class GlobalVariableManager : public QObject {
    Q_OBJECT
    
public:
    static GlobalVariableManager &instance();
    
    // 变量管理
    void setVariable(const QString &name, const QVariant &value);
    QVariant variable(const QString &name) const;
    bool hasVariable(const QString &name) const;
    void removeVariable(const QString &name);
    
    // 变量列表
    QStringList variableNames() const;
    int variableCount() const;
    
    // 变量类型
    QVariant::Type variableType(const QString &name) const;
    
    // 持久化
    bool saveToFile(const QString &path);
    bool loadFromFile(const QString &path);
    
signals:
    void variableAdded(const QString &name);
    void variableRemoved(const QString &name);
    void variableChanged(const QString &name, const QVariant &value);
};
```

### NodeFactory

节点工厂。

```cpp
class NodeFactory : public QObject {
    Q_OBJECT
    
public:
    static NodeFactory &instance();
    
    // 节点创建
    NodeBase *createNode(const QString &type, QObject *parent = nullptr);
    NodeBase *createNodeFromPalette(const QString &id, QObject *parent = nullptr);
    NodeBase *createNodeFromMime(const QString &mimeData, QObject *parent = nullptr);
    
    // 类型查询
    QStringList nodeTypes() const;
    QStringList nodeCategories() const;
    QStringList nodesInCategory(const QString &category) const;
    
    // 注册
    bool registerNode(const QString &type, 
                      std::function<NodeBase*(QObject*)> creator);
    bool unregisterNode(const QString &type);
    
    // 工具箱
    QToolBox *createToolBox(QWidget *parent = nullptr);
    
signals:
    void nodeRegistered(const QString &type);
    void nodeUnregistered(const QString &type);
};
```

### DataObject 工具函数

```cpp
namespace DataObjectUtils {
    // 创建 DataObject
    DataObject createImageObject(const HObject &image);
    DataObject createRegionObject(const HRegion &region);
    DataObject createNumberObject(double value);
    DataObject createStringObject(const QString &value);
    
    // 转换
    HObject toImage(const DataObject &obj);
    HRegion toRegion(const DataObject &obj);
    double toNumber(const DataObject &obj);
    QString toString(const DataObject &obj);
    
    // 类型检查
    bool isImage(const DataObject &obj);
    bool isRegion(const DataObject &obj);
    bool isNumber(const DataObject &obj);
    bool isString(const DataObject &obj);
    
    // 比较
    bool equals(const DataObject &a, const DataObject &b);
    bool isCompatible(PortDataType type, const DataObject &obj);
}
```

---

## 使用示例

### 创建自定义节点

```cpp
// 1. 定义节点类
class CustomNode : public HalconNode {
    Q_OBJECT
    
public:
    CustomNode(QObject *parent = nullptr) : HalconNode(parent) {}
    
    void init() override {
        addInputPort("图像", PortDataType::Image);
        addOutputPort("结果", PortDataType::Image);
        addOutputPort("区域", PortDataType::Region);
        
        m_params["param1"] = 100;
        m_params["param2"] = 0.5;
    }
    
    void run(bool autoSwitch = true) override {
        try {
            HObject input = getInputImage();
            if (!input.IsInitialized()) {
                setError("输入图像未初始化");
                return;
            }
            
            // 处理图像
            HObject output;
            threshold(input, &output, m_params["param1"].toInt(), 255);
            
            // 设置输出
            setOutputImage(output);
            
            // 创建区域对象
            HRegion region;
            connection(output, &region);
            
            auto regionObj = QSharedPointer<DataObject>(
                new DataObject(DataObject::Region, QVariant()));
            regionObj->setRegion(region);
            
            emit imageReady(output);
            
        } catch (const HException &e) {
            setError(QString("HALCON 错误: %1").arg(e.ErrorMessage()));
        }
    }
    
    QJsonObject toJson() const override {
        QJsonObject json;
        json["type"] = "CustomNode";
        json["param1"] = m_params["param1"].toInt();
        json["param2"] = m_params["param2"].toDouble();
        return json;
    }
    
    void fromJson(const QJsonObject &json) override {
        if (json.contains("param1")) {
            m_params["param1"] = json["param1"].toInt();
        }
        if (json.contains("param2")) {
            m_params["param2"] = json["param2"].toDouble();
        }
    }
    
    QWidget *createParamPanel() override {
        QWidget *panel = new QWidget();
        QFormLayout *layout = new QFormLayout(panel);
        
        QSpinBox *param1Spin = new QSpinBox();
        param1Spin->setRange(0, 255);
        param1Spin->setValue(m_params["param1"].toInt());
        connect(param1Spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int value) {
            setParam("param1", value);
        });
        layout->addRow("参数1:", param1Spin);
        
        return panel;
    }
};

// 2. 注册节点
NodeFactory::instance().registerNode("CustomNode", 
    [](QObject *parent) { return new CustomNode(parent); });

// 3. 使用节点
auto *node = NodeFactory::instance().createNode("CustomNode");
node->init();
node->run();
```

### 创建流程

```cpp
// 1. 创建场景
FlowScene scene;

// 2. 创建节点
auto *reader = new ImageReadNode();
auto *threshold = new ThresholdNode();
auto *display = new DisplaySinkNode();

// 3. 添加到场景
scene.addNode(reader);
scene.addNode(threshold);
scene.addNode(display);

// 4. 创建连接
scene.createConnection(reader->outputPort(0), threshold->inputPort(0));
scene.createConnection(threshold->outputPort(0), display->inputPort(0));

// 5. 配置参数
reader->setParam("imagePath", "test.png");
threshold->setParam("thresholdMin", 128);
threshold->setParam("thresholdMax", 255);

// 6. 执行流程
FlowExecutor executor;
executor.setScene(&scene);
executor.startExecution();
```

### 使用全局变量

```cpp
// 1. 设置变量
GlobalVariableManager::instance().setVariable("threshold", 128);
GlobalVariableManager::instance().setVariable("minArea", 100.0);

// 2. 获取变量
int threshold = GlobalVariableManager::instance()
    .variable("threshold").toInt();
double minArea = GlobalVariableManager::instance()
    .variable("minArea").toDouble();

// 3. 监听变量变化
connect(&GlobalVariableManager::instance(), 
        &GlobalVariableManager::variableChanged,
        [](const QString &name, const QVariant &value) {
    qDebug() << "变量" << name << "已更改为" << value;
});
```

---

## 错误处理

### HALCON 异常处理

```cpp
try {
    // HALCON 操作
    HObject image;
    read_image(&image, "test.png");
    
    HObject region;
    threshold(image, &region, 128, 255);
    
} catch (const HException &e) {
    // 获取错误信息
    HTuple errorCode, errorMsg;
    e.GetMessage(&errorCode, &errorMsg);
    
    QString error = QString("HALCON 错误 %1: %2")
        .arg(errorCode.I())
        .arg(QString::fromStdString(errorMsg.S()));
    
    qWarning() << error;
    setError(error);
}
```

### Qt 错误处理

```cpp
// 检查文件操作
QFile file("config.json");
if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "无法打开文件:" << file.errorString();
    return false;
}

// 检查 JSON 解析
QJsonParseError error;
QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
if (error.error != QJsonParseError::NoError) {
    qWarning() << "JSON 解析错误:" << error.errorString();
    return false;
}
```

---

## 最佳实践

1. **内存管理**：使用智能指针管理动态内存
2. **线程安全**：确保 UI 操作在主线程执行
3. **错误处理**：所有可能失败的操作都要有错误处理
4. **资源释放**：确保在析构函数中释放所有资源
5. **信号槽**：使用类型安全的信号槽连接
6. **序列化**：实现完整的 toJson/fromJson 方法
7. **文档注释**：为所有公开接口添加文档注释
8. **单元测试**：为关键功能编写单元测试
