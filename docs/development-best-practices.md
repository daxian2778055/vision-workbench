# VisionFlowPlatform 开发工作流最佳实践

## 概述

本文档描述了 VisionFlowPlatform 项目的开发工作流最佳实践，包括代码管理、开发流程、测试策略和部署流程。

---

## 1. 代码管理

### 1.1 Git 分支策略

#### 主要分支
- **main**: 生产分支，始终保持稳定
- **develop**: 开发分支，集成最新功能
- **feature/***: 功能分支，开发新功能
- **fix/***: 修复分支，修复 bug
- **release/***: 发布分支，准备发布

#### 分支命名规范
```
feature/add-new-node
feature/improve-performance
fix/memory-leak-in-flow-executor
fix/crash-on-startup
release/v1.0.0
hotfix/critical-bug
```

#### 分支工作流
```bash
# 1. 创建功能分支
git checkout develop
git pull origin develop
git checkout -b feature/my-feature

# 2. 开发功能
# ... 编写代码 ...

# 3. 提交更改
git add .
git commit -m "feat: add my feature"

# 4. 推送分支
git push origin feature/my-feature

# 5. 创建 Pull Request
# 在 GitHub 上创建 PR，目标分支为 develop

# 6. 代码审查
# 等待审查者审查代码

# 7. 合并代码
# 审查通过后合并到 develop

# 8. 删除功能分支
git branch -d feature/my-feature
git push origin --delete feature/my-feature
```

### 1.2 提交规范

#### 提交信息格式
```
<type>(<scope>): <subject>

<body>

<footer>
```

#### 类型 (type)
- **feat**: 新功能
- **fix**: 修复 bug
- **docs**: 文档更新
- **style**: 代码格式调整（不影响功能）
- **refactor**: 代码重构
- **perf**: 性能优化
- **test**: 测试相关
- **chore**: 构建/工具相关

#### 范围 (scope)
- **node**: 节点相关
- **flow**: 流程相关
- **ui**: 界面相关
- **comm**: 通信相关
- **camera**: 相机相关
- **test**: 测试相关

#### 示例
```
feat(node): add blob analysis node

- Add BlobAnalysisNode class
- Implement blob detection algorithm
- Add unit tests

Closes #123
```

```
fix(flow): fix memory leak in FlowExecutor

- Fix memory leak in node execution
- Add proper cleanup in destructor
- Add memory leak detection in tests

Fixes #456
```

### 1.3 代码审查

#### 审查清单
- [ ] 代码符合项目规范
- [ ] 无静态分析警告
- [ ] 测试覆盖率足够
- [ ] 文档已更新
- [ ] 性能无退化
- [ ] 无安全漏洞

#### 审查流程
1. 创建 Pull Request
2. 自动运行 CI/CD 检查
3. 同行审查代码
4. 修复审查意见
5. 合并到主分支

#### 审查要点
- **代码质量**: 命名规范、代码清晰、注释充分
- **内存管理**: 智能指针、RAII、无泄漏
- **线程安全**: 信号槽连接、互斥锁、原子操作
- **性能**: 避免不必要拷贝、优化算法
- **错误处理**: 异常捕获、错误传播、日志记录

---

## 2. 开发流程

### 2.1 功能开发流程

#### 步骤 1：需求分析
- 理解功能需求
- 确定技术方案
- 评估工作量

#### 步骤 2：设计
- 设计接口
- 设计数据结构
- 设计算法

#### 步骤 3：实现
- 编写代码
- 编写测试
- 编写文档

#### 步骤 4：测试
- 单元测试
- 集成测试
- 性能测试

#### 步骤 5：审查
- 自我审查
- 同行审查
- 专家审查

#### 步骤 6：合并
- 修复审查意见
- 合并到主分支
- 删除功能分支

### 2.2 Bug 修复流程

#### 步骤 1：重现问题
- 描述问题现象
- 确定重现步骤
- 收集日志信息

#### 步骤 2：定位问题
- 分析日志
- 调试代码
- 找到根本原因

#### 步骤 3：修复问题
- 编写修复代码
- 编写回归测试
- 验证修复效果

#### 步骤 4：提交修复
- 创建修复分支
- 提交修复代码
- 创建 Pull Request

#### 步骤 5：验证修复
- 运行测试
- 验证问题解决
- 合并到主分支

### 2.3 重构流程

#### 步骤 1：识别重构目标
- 找到代码异味
- 确定重构范围
- 评估重构风险

#### 步骤 2：编写测试
- 为现有代码编写测试
- 确保测试覆盖关键路径
- 验证测试通过

#### 步骤 3：执行重构
- 逐步重构
- 保持测试通过
- 避免引入新功能

#### 步骤 4：验证重构
- 运行所有测试
- 检查性能变化
- 检查代码质量

#### 步骤 5：提交重构
- 创建重构分支
- 提交重构代码
- 创建 Pull Request

---

## 3. 测试策略

### 3.1 测试金字塔

```
         /\
        /  \        E2E 测试 (10%)
       /    \       验证完整用户流程
      /------\
     /        \     集成测试 (20%)
    /          \    验证模块间交互
   /------------\
  /              \  单元测试 (70%)
 /                \ 验证单个函数/类
/------------------\
```

### 3.2 单元测试

#### 测试原则
- **独立性**: 每个测试独立运行
- **可重复**: 测试结果一致
- **快速**: 测试执行快速
- **自验证**: 测试自动判断通过/失败
- **及时**: 编写代码时同步编写测试

#### 测试覆盖
- **正常路径**: 测试正常输入
- **边界条件**: 测试边界值
- **异常路径**: 测试异常输入
- **性能**: 测试关键路径性能

#### 测试示例
```cpp
class ThresholdNodeTest : public QObject {
    Q_OBJECT
    
private slots:
    void testNormalThreshold();
    void testBoundaryValues();
    void testInvalidInput();
    void testPerformance();
};

void ThresholdNodeTest::testNormalThreshold() {
    ThresholdNode node;
    node.init();
    
    // 创建测试图像
    HObject image;
    gen_image_const(&image, "byte", 100, 100);
    
    node.setInputImage(image);
    node.setParam("thresholdMin", 128);
    node.setParam("thresholdMax", 255);
    
    QVERIFY(node.process());
    QVERIFY(node.getOutputImage().IsInitialized());
}

void ThresholdNodeTest::testBoundaryValues() {
    ThresholdNode node;
    node.init();
    
    // 测试边界值
    node.setParam("thresholdMin", 0);
    node.setParam("thresholdMax", 255);
    
    QCOMPARE(node.getParam("thresholdMin").toInt(), 0);
    QCOMPARE(node.getParam("thresholdMax").toInt(), 255);
}

void ThresholdNodeTest::testInvalidInput() {
    ThresholdNode node;
    node.init();
    
    // 测试无效输入
    HObject emptyImage;
    node.setInputImage(emptyImage);
    
    QVERIFY(!node.process());
}

void ThresholdNodeTest::testPerformance() {
    ThresholdNode node;
    node.init();
    
    HObject largeImage;
    gen_image_const(&largeImage, "byte", 4096, 4096);
    
    node.setInputImage(largeImage);
    
    QBENCHMARK {
        node.process();
    }
}
```

### 3.3 集成测试

#### 测试范围
- **节点间交互**: 测试节点连接和数据传递
- **流程执行**: 测试完整流程执行
- **系统集成**: 测试与外部系统集成

#### 测试示例
```cpp
class FlowExecutionTest : public QObject {
    Q_OBJECT
    
private slots:
    void testSimpleFlow();
    void testComplexFlow();
    void testErrorHandling();
};

void FlowExecutionTest::testSimpleFlow() {
    FlowScene scene;
    
    auto *reader = new ImageReadNode();
    auto *threshold = new ThresholdNode();
    auto *display = new DisplaySinkNode();
    
    scene.addNode(reader);
    scene.addNode(threshold);
    scene.addNode(display);
    
    scene.createConnection(reader->outputPort(0), threshold->inputPort(0));
    scene.createConnection(threshold->outputPort(0), display->inputPort(0));
    
    reader->setParam("imagePath", "test.png");
    threshold->setParam("thresholdMin", 128);
    
    FlowExecutor executor;
    executor.setScene(&scene);
    
    QSignalSpy finishedSpy(&executor, &FlowExecutor::executionFinished);
    executor.startExecution();
    
    QVERIFY(finishedSpy.wait(5000));
}
```

### 3.4 性能测试

#### 测试指标
- **执行时间**: 关键操作的执行时间
- **内存使用**: 内存占用和泄漏
- **CPU 使用**: CPU 占用率
- **吞吐量**: 单位时间处理量

#### 测试示例
```cpp
class PerformanceTest : public QObject {
    Q_OBJECT
    
private slots:
    void testThresholdPerformance();
    void testBlobPerformance();
    void testFlowPerformance();
};

void PerformanceTest::testThresholdPerformance() {
    ThresholdNode node;
    node.init();
    
    HObject image;
    gen_image_const(&image, "byte", 4096, 4096);
    node.setInputImage(image);
    
    QElapsedTimer timer;
    timer.start();
    
    for (int i = 0; i < 100; i++) {
        node.process();
    }
    
    qint64 elapsed = timer.elapsed();
    qDebug() << "Threshold performance:" << elapsed / 100.0 << "ms per image";
    
    QVERIFY(elapsed / 100.0 < 100);  // 应该小于 100ms
}
```

---

## 4. 代码质量

### 4.1 命名规范

#### 类名
```cpp
// PascalCase
class ThresholdNode;
class FlowExecutor;
class ImageReadNode;
```

#### 函数名
```cpp
// camelCase
void processImage();
bool isOpen() const;
void setParam(const QString &name, const QVariant &value);
```

#### 变量名
```cpp
// camelCase
int thresholdMin;
QString imagePath;
HObject outputImage;

// 成员变量 m_ 前缀
int m_thresholdMin;
QString m_imagePath;
HObject m_outputImage;
```

#### 常量名
```cpp
// UPPER_SNAKE_CASE
const int MAX_THRESHOLD = 255;
const QString DEFAULT_IMAGE_PATH = "test.png";
```

### 4.2 代码格式

#### 缩进
- 使用 4 个空格缩进
- 不使用 Tab

#### 大括号
```cpp
// 函数
void process() {
    // ...
}

// 类
class MyClass {
public:
    // ...
};

// 循环
for (int i = 0; i < 10; i++) {
    // ...
}

// 条件
if (condition) {
    // ...
} else {
    // ...
}
```

#### 空行
- 函数之间空一行
- 逻辑块之间空一行
- 不超过两个连续空行

### 4.3 注释规范

#### 文件头注释
```cpp
/**
 * @file ThresholdNode.h
 * @brief 阈值分割节点
 * @author Your Name
 * @date 2026-08-17
 */
```

#### 类注释
```cpp
/**
 * @brief 阈值分割节点
 * 
 * 对输入图像进行阈值分割，输出二值图像。
 * 
 * 参数：
 * - thresholdMin: 最小阈值 (0-255)
 * - thresholdMax: 最大阈值 (0-255)
 */
class ThresholdNode : public HalconNode {
    // ...
};
```

#### 函数注释
```cpp
/**
 * @brief 处理图像
 * 
 * 对输入图像进行阈值分割。
 * 
 * @param input 输入图像
 * @param output 输出图像
 * @param minThreshold 最小阈值
 * @param maxThreshold 最大阈值
 * @return true 处理成功
 * @return false 处理失败
 */
bool processImage(const HObject &input, HObject &output,
                  int minThreshold, int maxThreshold);
```

### 4.4 错误处理

#### 异常处理
```cpp
try {
    // HALCON 操作
    HObject image;
    read_image(&image, "test.png");
    
    HObject region;
    threshold(image, &region, 128, 255);
    
} catch (const HException &e) {
    // 处理 HALCON 异常
    qWarning() << "HALCON error:" << e.ErrorMessage();
    return false;
    
} catch (const std::exception &e) {
    // 处理标准异常
    qWarning() << "Standard error:" << e.what();
    return false;
    
} catch (...) {
    // 处理未知异常
    qWarning() << "Unknown error";
    return false;
}
```

#### 错误传播
```cpp
bool ThresholdNode::process() {
    try {
        HObject input = getInputImage();
        if (!input.IsInitialized()) {
            setError("Input image not initialized");
            return false;
        }
        
        // 处理图像
        HObject output;
        threshold(input, &output, m_thresholdMin, m_thresholdMax);
        
        setOutputImage(output);
        return true;
        
    } catch (const HException &e) {
        setError(QString("HALCON error: %1").arg(e.ErrorMessage()));
        return false;
    }
}
```

---

## 5. 性能优化

### 5.1 内存优化

#### 使用智能指针
```cpp
// 好
auto data = std::make_unique<DataObject>();

// 不好
DataObject *data = new DataObject();
```

#### 避免不必要的拷贝
```cpp
// 好：使用引用
void process(const HObject &image);

// 不好：值传递
void process(HObject image);
```

#### 使用移动语义
```cpp
HObject result = std::move(tempImage);
```

### 5.2 算法优化

#### 使用高效算法
```cpp
// 好：使用 HALCON 优化算法
threshold(image, &region, 128, 255);

// 不好：手动实现
for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
        // ...
    }
}
```

#### 并行处理
```cpp
// 使用 QtConcurrent
QtConcurrent::map(images, [](HObject &image) {
    processImage(image);
});
```

### 5.3 I/O 优化

#### 使用缓存
```cpp
class ImageCache {
    QMap<QString, HObject> m_cache;
    
    HObject getImage(const QString &path) {
        if (m_cache.contains(path)) {
            return m_cache[path];
        }
        
        HObject image;
        read_image(&image, path.toStdString().c_str());
        m_cache[path] = image;
        return image;
    }
};
```

#### 异步 I/O
```cpp
QtConcurrent::run([this]() {
    HObject image;
    read_image(&image, "large_image.png");
    
    QMetaObject::invokeMethod(this, [this, image]() {
        processImage(image);
    });
});
```

---

## 6. 安全开发

### 6.1 输入验证

```cpp
bool validateInput(const QString &input) {
    // 检查长度
    if (input.length() > 1000) {
        qWarning() << "Input too long";
        return false;
    }
    
    // 检查特殊字符
    if (input.contains(QRegularExpression("[<>\"']"))) {
        qWarning() << "Invalid characters in input";
        return false;
    }
    
    return true;
}
```

### 6.2 内存安全

```cpp
// 使用智能指针
auto data = std::make_unique<DataObject>();

// 检查空指针
if (ptr == nullptr) {
    qWarning() << "Null pointer";
    return false;
}

// 检查数组边界
if (index < 0 || index >= array.size()) {
    qWarning() << "Index out of bounds";
    return false;
}
```

### 6.3 线程安全

```cpp
// 使用互斥锁
QMutex mutex;

void threadSafeFunction() {
    QMutexLocker locker(&mutex);
    // 访问共享数据
}

// 使用原子操作
QAtomicInt counter;
counter.fetchAndAddRelaxed(1);
```

---

## 7. 文档规范

### 7.1 代码文档

#### 头文件文档
```cpp
/**
 * @file MyNode.h
 * @brief 我的自定义节点
 * @author Your Name
 * @date 2026-08-17
 * @version 1.0
 */

#pragma once

#include "HalconNode.h"

/**
 * @brief 我的自定义节点
 * 
 * 实现自定义图像处理功能。
 */
class MyNode : public HalconNode {
    Q_OBJECT
    
public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit MyNode(QObject *parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~MyNode() override = default;
    
    /**
     * @brief 初始化节点
     * 
     * 初始化端口和参数。
     */
    void init() override;
    
    /**
     * @brief 处理图像
     * 
     * 对输入图像进行处理。
     * 
     * @return true 处理成功
     * @return false 处理失败
     */
    bool process() override;
};
```

### 7.2 API 文档

#### 使用 Doxygen
```bash
# 生成文档
doxygen Doxyfile

# 查看文档
open docs/html/index.html
```

#### Doxygen 配置
```bash
# Doxyfile
PROJECT_NAME = "VisionFlowPlatform"
PROJECT_BRIEF = "工业视觉流程开发平台"
OUTPUT_DIRECTORY = docs
INPUT = include src
RECURSIVE = YES
GENERATE_HTML = YES
GENERATE_LATEX = NO
```

### 7.3 用户文档

#### README.md
```markdown
# VisionFlowPlatform

工业视觉流程开发平台

## 功能特性

- 可视化流程编辑器
- 多种图像处理算法
- 工业相机集成
- 通信协议支持

## 快速开始

1. 克隆项目
2. 安装依赖
3. 构建项目
4. 运行程序

## 文档

- [API 文档](docs/api-reference.md)
- [快速入门](docs/quick-start-guide.md)
- [故障排除](docs/troubleshooting-guide.md)
```

---

## 8. 持续集成

### 8.1 CI/CD 流程

```
代码提交 → 静态分析 → 构建 → 测试 → 部署
```

### 8.2 自动化检查

- **静态分析**: cppcheck、clang-tidy
- **代码质量**: 复杂度分析、重复代码检测
- **测试覆盖率**: 单元测试、集成测试
- **安全扫描**: 漏洞检测

### 8.3 自动化部署

- **构建**: 自动生成构建产物
- **测试**: 自动运行测试套件
- **部署**: 自动部署到测试/生产环境

---

## 9. 团队协作

### 9.1 沟通规范

#### 代码审查
- 提供建设性反馈
- 解释问题原因
- 提供修复建议

#### 问题报告
- 描述问题现象
- 提供重现步骤
- 附上日志和截图

#### 知识分享
- 编写技术文档
- 组织技术分享
- 记录最佳实践

### 9.2 协作工具

- **Git**: 版本控制
- **GitHub**: 代码托管、Issue 跟踪
- **Slack**: 即时通讯
- **Confluence**: 文档协作
- **Jira**: 项目管理

---

## 10. 总结

通过遵循这些最佳实践，可以：

1. **提高代码质量**: 规范的代码风格、完善的测试覆盖
2. **提高开发效率**: 标准化的开发流程、自动化工具
3. **降低维护成本**: 清晰的文档、良好的代码结构
4. **增强团队协作**: 统一的规范、有效的沟通

持续改进和优化开发工作流，将帮助团队更好地交付高质量的软件产品。
