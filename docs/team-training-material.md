# VisionFlowPlatform 开发工具培训材料

## 培训概述

本培训材料旨在帮助团队成员掌握 VisionFlowPlatform 项目中新增的开发工具和流程，提高开发效率和代码质量。

## 培训目标

1. 掌握 mcp-cpp 工具的使用
2. 掌握静态分析工具（cppcheck、clang-tidy）的使用
3. 理解并遵循代码审查流程
4. 掌握 API 文档的使用方法

## 培训内容

### 第一部分：mcp-cpp 工具培训

#### 1.1 什么是 mcp-cpp？

mcp-cpp 是一个 MCP（Model Context Protocol）服务器，通过 clangd LSP 集成为 C++ 代码分析提供语义理解能力。

**主要功能**：
- 项目结构分析
- 符号搜索（类、函数、变量）
- 符号上下文分析（继承关系、调用层次）
- 构建配置切换

#### 1.2 安装和配置

**安装步骤**：
```bash
# 1. 安装 Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# 2. 安装 mcp-cpp-server
cargo install mcp-cpp-server

# 3. 验证安装
mcp-cpp-server --version
```

**配置 Cursor**：
在 `.cursor/mcp.json` 中添加：
```json
{
  "mcpServers": {
    "cpp-tools": {
      "command": "mcp-cpp-server",
      "args": ["--root", "E:\\halcon\\2\\xin1"],
      "env": {
        "CLANGD_PATH": "C:\\Program Files\\LLVM\\bin\\clangd.exe"
      }
    }
  }
}
```

#### 1.3 基本使用

**获取项目详情**：
```json
{ "name": "get_project_details" }
```

**搜索符号**：
```json
{
  "name": "search_symbols",
  "arguments": { "query": "NodeBase" }
}
```

**分析符号上下文**：
```json
{
  "name": "analyze_symbol_context",
  "arguments": { "symbol": "FlowExecutor::startExecution" }
}
```

#### 1.4 实践练习

**练习 1：搜索项目中的所有节点类**
1. 使用 `search_symbols` 搜索 "Node"
2. 分析搜索结果
3. 找出所有继承自 NodeBase 的类

**练习 2：分析 FlowExecutor 类**
1. 使用 `analyze_symbol_context` 分析 FlowExecutor
2. 查看其方法和属性
3. 理解其执行流程

---

### 第二部分：静态分析工具培训

#### 2.1 什么是静态分析？

静态分析是在不执行程序的情况下分析代码，以发现潜在问题。

**常见问题类型**：
- 内存泄漏
- 空指针解引用
- 未初始化变量
- 缓冲区溢出
- 性能问题

#### 2.2 cppcheck 使用

**基本用法**：
```bash
# 分析整个项目
cppcheck --enable=all --suppress=missingInclude src/ include/

# 分析特定文件
cppcheck --enable=all src/MainWindow.cpp

# 生成 XML 报告
cppcheck --enable=all --xml --xml-version=2 src/ include/ 2> results.xml
```

**常见检查项**：
- 内存泄漏
- 空指针解引用
- 未初始化变量
- 缓冲区溢出
- 整数溢出

#### 2.3 clang-tidy 使用

**基本用法**：
```bash
# 分析整个项目
clang-tidy -p build src/*.cpp

# 分析特定文件
clang-tidy -p build src/MainWindow.cpp

# 自动修复
clang-tidy -p build --fix src/MainWindow.cpp
```

**常见检查类别**：
- `clang-analyzer-*`：Clang 静态分析器
- `cppcoreguidelines-*`：C++ 核心指南
- `modernize-*`：现代 C++ 特性
- `performance-*`：性能优化
- `readability-*`：代码可读性

#### 2.4 集成到开发流程

**Git 预提交钩子**：
```bash
#!/bin/bash
# .git/hooks/pre-commit

files=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|h)$')

if [ -z "$files" ]; then
    exit 0
fi

# 运行 cppcheck
cppcheck --enable=all --suppress=missingInclude --error-exitcode=1 $files

# 运行 clang-tidy
clang-tidy -p build $files

exit 0
```

#### 2.5 实践练习

**练习 1：使用 cppcheck 分析代码**
1. 对 `src/MainWindow.cpp` 运行 cppcheck
2. 分析检查结果
3. 修复发现的问题

**练习 2：使用 clang-tidy 分析代码**
1. 对 `src/FlowExecutor.cpp` 运行 clang-tidy
2. 分析检查结果
3. 使用自动修复功能

---

### 第三部分：代码审查流程培训

#### 3.1 代码审查的重要性

**代码审查的目标**：
- 提高代码质量
- 发现潜在问题
- 知识共享
- 团队协作

**代码审查的好处**：
- 减少 bug
- 提高代码可维护性
- 促进团队学习
- 建立编码标准

#### 3.2 审查流程

**提交前准备**：
1. 代码编译通过，无警告
2. 单元测试通过
3. 静态分析工具检查通过
4. 代码符合项目编码规范

**审查请求**：
1. 填写审查请求模板
2. 描述变更内容
3. 说明影响范围
4. 提供测试情况

**审查过程**：
1. 审查者检查代码质量
2. 审查者提供反馈
3. 开发者响应反馈
4. 修复问题

**审查完成**：
1. 所有严重问题已修复
2. 审查者已批准
3. CI/CD 流水线通过

#### 3.3 审查检查清单

**代码质量**：
- [ ] 命名规范是否符合项目标准
- [ ] 代码是否清晰易懂
- [ ] 函数长度是否合理（<50行）
- [ ] 是否有重复代码

**内存管理**：
- [ ] 是否正确使用智能指针
- [ ] 是否有内存泄漏风险
- [ ] Qt对象父子关系是否正确

**线程安全**：
- [ ] 跨线程信号槽是否使用正确连接类型
- [ ] UI操作是否在主线程
- [ ] 共享数据是否有适当保护

**性能**：
- [ ] 是否有不必要的拷贝
- [ ] 容器选择是否合适
- [ ] 算法复杂度是否合理

**错误处理**：
- [ ] HALCON调用是否有异常处理
- [ ] 文件操作是否有错误检查
- [ ] 错误信息是否有用

#### 3.4 审查评论规范

**评论类型**：
- **严重问题**：必须修复，阻塞合并
- **警告**：建议修复，不阻塞合并
- **建议**：可选优化，供参考
- **问题**：需要澄清的疑问
- **肯定**：好的实践，值得学习

**评论格式**：
```markdown
**[类型]** 位置：文件:行号

**问题描述**：
[清晰描述问题]

**建议修复**：
```cpp
// 修复代码示例
```

**原因**：
[解释为什么需要修复]
```

#### 3.5 实践练习

**练习 1：模拟代码审查**
1. 选择一段代码
2. 按照审查清单进行检查
3. 编写审查评论
4. 讨论审查结果

**练习 2：响应审查反馈**
1. 收到审查反馈
2. 分析反馈内容
3. 修复问题
4. 编写反馈响应

---

### 第四部分：API 文档培训

#### 4.1 API 文档结构

**文档位置**：`E:\halcon\2\xin1\docs\api-reference.md`

**文档内容**：
1. 核心基类
2. 节点接口
3. 数据类型
4. 端口系统
5. 流程管理
6. 相机接口
7. 通信接口
8. 工具类

#### 4.2 核心基类

**NodeBase**：
所有算子节点的抽象基类。

```cpp
class NodeBase : public QObject {
    Q_OBJECT
    
public:
    // 纯虚函数 - 必须实现
    virtual void init() = 0;
    virtual void run(bool autoSwitch = true) = 0;
    virtual void setParam(const QString &name, const QVariant &value) = 0;
    virtual QVariant getParam(const QString &name) const = 0;
    virtual void drawResult() = 0;
    virtual QJsonObject toJson() const = 0;
    virtual void fromJson(const QJsonObject &json) = 0;
    
    // 端口管理
    void addInputPort(const QString &name, PortDataType type);
    void addOutputPort(const QString &name, PortDataType type);
    
    // 数据获取
    HObject getInputImage() const;
    void setOutputImage(const HObject &image);
};
```

**HalconNode**：
HALCON 算子的中间基类。

```cpp
class HalconNode : public NodeBase {
    Q_OBJECT
    
public:
    // HALCON 特定方法
    HalconWindow *getHalconWindow() const;
    void setHalconWindow(HalconWindow *window);
    
    // 结果绘制
    void drawRegion(const HRegion &region, const QString &color = "green");
    void drawContour(const HXLD &contour, const QString &color = "red");
};
```

#### 4.3 节点接口

**图像采集节点**：
- MvsImageSourceNode：海康 MVS 相机
- HalconImageSourceNode：HALCON 图像文件

**图像处理节点**：
- ThresholdNode：阈值分割
- BlurNode：图像模糊
- MorphologyNode：形态学操作

**分析节点**：
- BlobAnalysisNode：Blob 分析
- EdgeDetectionNode：边缘检测

**测量节点**：
- FindLineNode：直线查找
- FindCircleNode：圆查找

#### 4.4 数据类型

**DataObject**：
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
    
    // 数据获取
    HObject image() const;
    HRegion region() const;
    double number() const;
    QString string() const;
};
```

#### 4.5 流程管理

**FlowScene**：
流程画布场景。

```cpp
class FlowScene : public QGraphicsScene {
    Q_OBJECT
    
public:
    // 节点管理
    void addNode(NodeBase *node);
    void removeNode(NodeBase *node);
    QList<NodeBase*> nodes() const;
    
    // 连线管理
    Connection *createConnection(Port *source, Port *target);
    void removeConnection(Connection *connection);
    
    // 编辑锁定
    void setEditLocked(bool locked);
    bool isEditLocked() const;
};
```

**FlowExecutor**：
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
    
    // 流程控制
    void startExecution();
    void stopExecution();
    void pauseExecution();
    void resumeExecution();
    
    // 模式设置
    void setFlowMode(FlowMode mode);
    FlowMode flowMode() const;
};
```

#### 4.6 实践练习

**练习 1：创建自定义节点**
1. 继承 HalconNode
2. 实现 init() 方法
3. 实现 run() 方法
4. 注册到 NodeFactory

**练习 2：使用 API 文档**
1. 查找 ThresholdNode 的 API
2. 理解其参数和方法
3. 编写使用示例

---

### 第五部分：综合实践

#### 5.1 项目实战

**任务 1：图像处理流程**
1. 创建图像读取节点
2. 创建阈值分割节点
3. 创建 Blob 分析节点
4. 创建显示节点
5. 连接节点并执行

**任务 2：代码审查实践**
1. 选择一段代码
2. 运行静态分析工具
3. 进行代码审查
4. 修复问题

#### 5.2 工具集成实践

**任务 1：配置 mcp-cpp**
1. 安装 mcp-cpp-server
2. 配置 Cursor
3. 测试工具功能

**任务 2：配置静态分析**
1. 安装 cppcheck 和 clang-tidy
2. 配置 CMakeLists.txt
3. 运行静态分析

#### 5.3 流程优化实践

**任务 1：优化代码审查流程**
1. 分析当前流程
2. 识别改进点
3. 实施优化措施

**任务 2：集成 CI/CD**
1. 配置 GitHub Actions
2. 添加静态分析步骤
3. 自动化测试

---

## 培训资源

### 文档资源
1. [mcp-cpp 安装指南](mcp-cpp-installation-guide.md)
2. [静态分析集成指南](static-analysis-integration.md)
3. [代码审查流程指南](code-review-process.md)
4. [API 参考文档](api-reference.md)

### 工具资源
1. [mcp-cpp GitHub](https://github.com/mpsm/mcp-cpp)
2. [cppcheck 官网](http://cppcheck.net)
3. [clang-tidy 文档](https://clang.llvm.org/extra/clang-tidy/)

### 学习资源
1. [C++ 核心指南](https://isocpp.github.io/CppCoreGuidelines/)
2. [Qt 文档](https://doc.qt.io/)
3. [HALCON 文档](https://www.mvtec.com/products/halcon/documentation/)

---

## 培训评估

### 评估方式
1. **理论测试**：测试培训内容的掌握程度
2. **实践操作**：测试工具使用能力
3. **代码审查**：测试代码审查能力
4. **项目实战**：测试综合应用能力

### 评估标准
1. **优秀**：熟练掌握所有工具和流程
2. **良好**：基本掌握工具和流程
3. **合格**：了解工具和流程
4. **不合格**：需要进一步培训

---

## 常见问题解答

### Q1: mcp-cpp 安装失败怎么办？
A1: 检查 Rust 是否正确安装，确保网络连接正常，尝试使用国内镜像。

### Q2: cppcheck 报告太多警告怎么办？
A2: 先修复严重问题，然后根据项目需求调整检查规则。

### Q3: 代码审查应该关注哪些方面？
A3: 主要关注代码质量、内存管理、线程安全、性能和错误处理。

### Q4: 如何提高代码审查效率？
A4: 使用自动化工具进行初步检查，然后进行人工审查。

### Q5: API 文档如何更新？
A5: 当代码变更时，同步更新 API 文档，确保文档与代码一致。

---

## 培训计划

### 第一周：工具安装和基础培训
- **周一**：mcp-cpp 安装和配置
- **周二**：静态分析工具安装和配置
- **周三**：API 文档学习
- **周四**：代码审查流程学习
- **周五**：实践练习

### 第二周：进阶培训和实战
- **周一**：mcp-cpp 高级功能
- **周二**：静态分析高级应用
- **周三**：代码审查实战
- **周四**：项目实战
- **周五**：总结和评估

### 第三周：优化和改进
- **周一**：流程优化
- **周二**：工具集成
- **周三**：CI/CD 配置
- **周四**：持续改进
- **周五**：最终评估

---

## 培训总结

通过本次培训，团队成员将掌握：

1. **mcp-cpp 工具**：C++ 代码分析、符号搜索、上下文分析
2. **静态分析工具**：cppcheck、clang-tidy 的使用和集成
3. **代码审查流程**：标准化的审查流程和最佳实践
4. **API 文档**：项目 API 的使用和参考

这些工具和流程将帮助团队：
- 提高代码质量
- 减少 bug 和安全问题
- 提高开发效率
- 促进团队协作

---

## 附录

### 附录 A：安装检查清单
- [ ] Rust 安装成功
- [ ] LLVM/clang-tidy 安装成功
- [ ] cppcheck 安装成功
- [ ] mcp-cpp-server 安装成功
- [ ] compile_commands.json 生成成功
- [ ] Cursor 配置完成

### 附录 B：工具版本要求
- Rust: 2024 版本或更高
- clangd: 11 或更高版本（推荐 20+）
- cppcheck: 最新版本
- CMake: 3.16 或更高

### 附录 C：常见错误和解决方案
1. **Rust 安装失败**：检查网络连接，使用国内镜像
2. **clang-tidy 找不到**：检查 LLVM 安装路径
3. **compile_commands.json 不存在**：重新生成 CMake 构建文件
4. **mcp-cpp 启动失败**：检查 clangd 路径配置

### 附录 D：参考资源链接
1. [Rust 官网](https://www.rust-lang.org/)
2. [LLVM 官网](https://llvm.org/)
3. [cppcheck GitHub](https://github.com/danmar/cppcheck)
4. [mcp-cpp GitHub](https://github.com/mpsm/mcp-cpp)
