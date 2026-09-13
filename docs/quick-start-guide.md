# VisionFlowPlatform 快速入门指南

## 5分钟快速开始

### 前置条件

- Windows 10/11 (64位)
- Visual Studio 2019 或更高版本
- Qt 6.10.0
- HALCON 24.11
- Git

### 第一步：克隆项目

```bash
git clone https://github.com/your-org/VisionFlowPlatform.git
cd VisionFlowPlatform
```

### 第二步：安装开发工具

```powershell
# 以管理员身份运行 PowerShell
cd E:\halcon\2\xin1
.\tools\install-dev-tools.ps1
```

### 第三步：配置环境

```powershell
# 设置环境变量
$env:HALCON_ROOT = "D:\Program Files\MVTec\HALCON-24.11-Progress-Steady"
$env:HALCONARCH = "x64-win64"
$env:OpenCV_DIR = "E:\halcon\2\xin1\thirdparty\opencv\build"
```

### 第四步：构建项目

```bash
# 生成构建文件
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 编译项目
cmake --build build --config Release --parallel
```

### 第五步：运行程序

```bash
# 运行主程序
.\build\bin\VisionFlowPlatform.exe

# 运行测试
cd build
ctest --output-on-failure
```

---

## 项目结构

```
VisionFlowPlatform/
├── include/                    # 头文件
│   ├── NodeBase.h             # 节点基类
│   ├── HalconNode.h           # HALCON 节点基类
│   ├── FlowScene.h            # 流程场景
│   ├── FlowExecutor.h         # 流程执行器
│   └── ...                    # 其他头文件
├── src/                        # 源文件
│   ├── main.cpp               # 程序入口
│   ├── MainWindow.cpp         # 主窗口
│   ├── NodeBase.cpp           # 节点基类实现
│   └── ...                    # 其他源文件
├── ui/                         # UI 文件
│   └── MainWindow.ui          # 主窗口 UI
├── tests/                      # 测试文件
├── thirdparty/                 # 第三方库
│   ├── opencv/                # OpenCV 4.13.0
│   ├── zxing/                 # ZXing 二维码库
│   └── tesseract/             # Tesseract OCR
├── docs/                       # 文档
├── tools/                      # 工具脚本
├── .cursor/                    # Cursor 配置
│   ├── rules/                 # 开发规则
│   └── mcp.json               # MCP 配置
├── CMakeLists.txt              # CMake 配置
└── 需求.txt                    # 需求文档
```

---

## 核心概念

### 1. 节点 (Node)

节点是流程中的基本单元，每个节点实现特定功能。

```cpp
// 示例：阈值分割节点
class ThresholdNode : public HalconNode {
    Q_OBJECT
    
public:
    void init() override {
        addInputPort("图像", PortDataType::Image);
        addOutputPort("结果", PortDataType::Image);
        m_params["thresholdMin"] = 0;
        m_params["thresholdMax"] = 128;
    }
    
    bool process() override {
        HObject input = getInputImage();
        HObject output;
        threshold(input, &output, 
                  m_params["thresholdMin"].toInt(),
                  m_params["thresholdMax"].toInt());
        setOutputImage(output);
        return true;
    }
};
```

### 2. 端口 (Port)

端口用于节点间的数据传递。

```cpp
// 输入端口：接收数据
addInputPort("图像", PortDataType::Image);

// 输出端口：发送数据
addOutputPort("结果", PortDataType::Image);
addOutputPort("区域", PortDataType::Region);
```

### 3. 连线 (Connection)

连线连接两个端口，实现数据流动。

```cpp
// 创建连线
scene->createConnection(sourcePort, targetPort);
```

### 4. 流程 (Flow)

流程是由节点和连线组成的有向图。

```cpp
// 创建流程
FlowScene scene;
scene.addNode(readerNode);
scene.addNode(processNode);
scene.addNode(displayNode);
scene.createConnection(readerNode->outputPort(0), 
                       processNode->inputPort(0));
```

### 5. 执行器 (Executor)

执行器负责执行流程。

```cpp
// 执行流程
FlowExecutor executor;
executor.setScene(&scene);
executor.startExecution();
```

---

## 常用操作

### 1. 创建新节点

```bash
# 1. 创建头文件
# include/MyNode.h

# 2. 创建源文件
# src/MyNode.cpp

# 3. 注册到 NodeFactory
# src/NodeFactory.cpp

# 4. 注册到 CMakeLists.txt
# CMakeLists.txt
```

### 2. 添加依赖库

```cmake
# 在 CMakeLists.txt 中添加
find_package(OpenCV REQUIRED)
target_link_libraries(VisionFlowPlatform ${OpenCV_LIBS})
```

### 3. 运行测试

```bash
# 运行所有测试
cd build
ctest --output-on-failure

# 运行特定测试
ctest -R ThresholdNodeTest
```

### 4. 生成文档

```bash
# 使用 Doxygen 生成文档
doxygen Doxyfile
```

---

## 开发流程

### 1. 功能开发

```bash
# 1. 创建功能分支
git checkout -b feature/my-feature

# 2. 编写代码
# ...

# 3. 运行测试
cd build
ctest --output-on-failure

# 4. 提交代码
git add .
git commit -m "feat: add my feature"

# 5. 推送分支
git push origin feature/my-feature

# 6. 创建 Pull Request
```

### 2. Bug 修复

```bash
# 1. 创建修复分支
git checkout -b fix/my-bug-fix

# 2. 修复 Bug
# ...

# 3. 编写测试
# ...

# 4. 提交代码
git add .
git commit -m "fix: fix my bug"

# 5. 推送分支
git push origin fix/my-bug-fix

# 6. 创建 Pull Request
```

### 3. 代码审查

```bash
# 1. 运行静态分析
cppcheck --enable=all src/ include/
clang-tidy -p build src/*.cpp

# 2. 检查代码质量
# - 命名规范
# - 内存管理
# - 线程安全
# - 错误处理

# 3. 创建 Pull Request
# 4. 等待审查
# 5. 修复问题
# 6. 合并代码
```

---

## 常见问题

### Q1: 构建失败怎么办？

```bash
# 检查依赖库是否安装
ls thirdparty/opencv/build
ls "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady"

# 清理构建目录
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Q2: 找不到 HALCON 怎么办？

```powershell
# 设置环境变量
$env:HALCON_ROOT = "D:\Program Files\MVTec\HALCON-24.11-Progress-Steady"
$env:HALCONARCH = "x64-win64"

# 或者在 CMake 中指定
cmake -B build -DHALCON_ROOT="D:/Program Files/MVTec/HALCON-24.11-Progress-Steady"
```

### Q3: 测试失败怎么办？

```bash
# 查看详细输出
ctest --output-on-failure

# 运行特定测试
ctest -R ThresholdNodeTest --verbose

# 检查测试代码
# 检查测试数据
# 检查测试环境
```

### Q4: 如何调试？

```bash
# 使用 Visual Studio 调试
# 1. 打开 build/VisionFlowPlatform.sln
# 2. 设置断点
# 3. 按 F5 启动调试

# 使用 Qt Creator 调试
# 1. 打开 CMakeLists.txt
# 2. 配置构建
# 3. 设置断点
# 4. 按 F5 启动调试
```

---

## 学习资源

### 官方文档

- [Qt 文档](https://doc.qt.io/)
- [HALCON 文档](https://www.mvtec.com/products/halcon/documentation/)
- [OpenCV 文档](https://docs.opencv.org/)
- [CMake 文档](https://cmake.org/documentation/)

### 项目文档

- [API 参考文档](api-reference.md)
- [代码审查流程](code-review-process.md)
- [静态分析集成](static-analysis-integration.md)
- [CI/CD 使用指南](ci-cd-guide.md)

### 培训材料

- [团队培训材料](team-training-material.md)
- [快速入门视频](#) (待创建)
- [最佳实践指南](#) (待创建)

---

## 获取帮助

### 1. 查看文档

```bash
# 查看项目文档
ls docs/

# 查看 README
cat README.md
```

### 2. 搜索问题

```bash
# 在 GitHub 上搜索问题
# https://github.com/your-org/VisionFlowPlatform/issues

# 在 Stack Overflow 上搜索
# https://stackoverflow.com/questions/tagged/visionflowplatform
```

### 3. 联系团队

- **邮件**: team@example.com
- **Slack**: #visionflowplatform
- **GitHub Issues**: [创建 Issue](https://github.com/your-org/VisionFlowPlatform/issues/new)

---

## 下一步

1. **阅读 API 文档**: 了解项目的核心 API
2. **运行示例程序**: 学习如何使用项目
3. **创建第一个节点**: 实践节点开发
4. **参与代码审查**: 学习最佳实践
5. **贡献代码**: 为项目做出贡献

---

## 快速参考

### 常用命令

```bash
# 构建项目
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel

# 运行测试
cd build
ctest --output-on-failure

# 静态分析
cppcheck --enable=all src/ include/
clang-tidy -p build src/*.cpp

# 生成文档
doxygen Doxyfile
```

### 常用快捷键

- **Ctrl+S**: 保存文件
- **Ctrl+Z**: 撤销
- **Ctrl+Y**: 重做
- **Ctrl+F**: 查找
- **Ctrl+H**: 替换
- **F5**: 启动调试
- **F9**: 切换断点
- **F10**: 逐过程
- **F11**: 逐语句

### 常用文件

- **CMakeLists.txt**: CMake 配置
- **include/*.h**: 头文件
- **src/*.cpp**: 源文件
- **ui/*.ui**: UI 文件
- **tests/*.cpp**: 测试文件
- **.cursor/rules/*.mdc**: 开发规则

---

## 总结

通过本快速入门指南，您应该能够：

1. **快速搭建开发环境**
2. **理解项目核心概念**
3. **掌握常用开发操作**
4. **了解开发流程**
5. **解决常见问题**

如有任何问题，请随时联系团队或查看详细文档。

**祝您开发愉快！**
