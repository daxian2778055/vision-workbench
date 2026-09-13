# VisionFlowPlatform 快速参考卡片

## 常用命令

### 构建命令
```bash
# Release 构建
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel

# Debug 构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel

# 指定 HALCON 路径
cmake -B build -DHALCON_ROOT="D:/Program Files/MVTec/HALCON-24.11-Progress-Steady"

# 生成 compile_commands.json
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### 测试命令
```bash
# 运行所有测试
cd build
ctest --output-on-failure

# 运行特定测试
ctest -R ThresholdNodeTest

# 详细输出
ctest --output-on-failure --verbose
```

### 静态分析
```bash
# cppcheck
cppcheck --enable=all src/ include/

# clang-tidy
clang-tidy -p build src/*.cpp

# 代码复杂度
lizard src/
```

### 工具脚本
```powershell
# 项目状态检查
.\tools\project-status.ps1

# 配置验证
.\tools\verify-config.ps1

# 健康检查
.\tools\health-check.ps1

# 安装开发工具
.\tools\install-dev-tools.ps1
```

---

## 项目路径

### 核心目录
```
项目根目录:     E:\halcon\2\xin1
源代码目录:     src/
头文件目录:     include/
UI 文件目录:    ui/
测试目录:       tests/
文档目录:       docs/
工具目录:       tools/
```

### 配置目录
```
Cursor 配置:    .cursor/
规则文件:       .cursor/rules/
SKILLS 文件:    .cursor/skills/
MCP 配置:       .cursor/mcp.json
CI/CD 配置:     .github/workflows/
```

### 第三方库
```
OpenCV:         thirdparty/opencv/
ZXing:          thirdparty/zxing/
Tesseract:      thirdparty/tesseract/
```

---

## 常用文件

### 核心文件
```
CMakeLists.txt              # CMake 配置
README.md                   # 项目说明
.gitignore                  # Git 忽略文件
需求.txt                    # 需求文档
```

### 规则文件
```
.cursor/rules/project-architecture.mdc   # 项目架构
.cursor/rules/node-development.mdc       # 算子开发
.cursor/rules/halcon-patterns.mdc        # HALCON 集成
.cursor/rules/code-style.mdc             # 代码风格
.cursor/rules/opencv-patterns.mdc        # OpenCV 集成
.cursor/rules/deep-learning.mdc          # 深度学习
.cursor/rules/testing.mdc                # 测试规范
```

### SKILLS 文件
```
.cursor/skills/halcon-node-development/SKILL.md   # HALCON 算子开发
.cursor/skills/cpp-code-review/SKILL.md           # C++ 代码审查
.cursor/skills/industrial-vision-system/SKILL.md  # 工业视觉系统
```

---

## 常用类和函数

### 核心基类
```cpp
NodeBase           // 节点基类
HalconNode         // HALCON 节点基类
FlowScene          // 流程场景
FlowExecutor       // 流程执行器
```

### 数据类型
```cpp
DataObject         // 数据对象
PortDataType       // 端口数据类型
Port               // 端口
Connection         // 连线
```

### 管理器
```cpp
GlobalCameraManager      // 全局相机管理器
GlobalVariableManager    // 全局变量管理器
NodeFactory              // 节点工厂
ProjectManager           // 项目管理器
```

---

## 命名规范

### 类命名
```cpp
class ThresholdNode;      // PascalCase
class FlowExecutor;       // PascalCase
class ImageReadNode;      // PascalCase
```

### 函数命名
```cpp
void processImage();      // camelCase
bool isOpen() const;      // camelCase
void setParam();          // camelCase
```

### 变量命名
```cpp
int thresholdMin;         // camelCase
QString imagePath;        // camelCase
HObject outputImage;      // camelCase

// 成员变量
int m_thresholdMin;       // m_ 前缀
QString m_imagePath;      // m_ 前缀
```

### 常量命名
```cpp
const int MAX_THRESHOLD = 255;    // UPPER_SNAKE_CASE
const QString DEFAULT_PATH = "";  // UPPER_SNAKE_CASE
```

---

## 常用 HALCON 算子

### 图像处理
```cpp
// 读取图像
read_image(&image, "test.png");

// 灰度转换
rgb1_to_gray(image, &grayImage);

// 高斯滤波
gauss_filter(image, &filtered, 5);

// 阈值分割
threshold(image, &region, 128, 255);

// 连通域
connection(region, &connectedRegions);

// 区域选择
select_shape(connectedRegions, &selected, "area", "and", 100, 99999);
```

### 模板匹配
```cpp
// NCC 匹配
create_ncc_model(modelImage, "auto", -90, 180, "auto", "use_polarity", &modelId);
find_ncc_model(searchImage, modelId, -90, 180, 0.5, 1, 0.5, "true", 0, &row, &col, &angle, &score);

// 形状匹配
create_shape_model(modelImage, "auto", -90, 180, "auto", "auto", "use_polarity", "auto", "auto", &modelId);
find_shape_model(searchImage, modelId, -90, 180, 0.5, 1, 0.5, "least_squares", 0, 0.9, &row, &col, &angle, &score);
```

### 测量
```cpp
// 直线拟合
fit_line_contour_xld(contour, "tukey", -1, 0, 5, 2, &rowBegin, &colBegin, &rowEnd, &colEnd, &nr, &nc, &dist);

// 圆拟合
fit_circle_contour_xld(contour, "algebraic", -1, 0, 0, 3, 2, &row, &column, &radius, &startPhi, &endPhi, &pointOrder);
```

---

## 常用 Qt 函数

### 信号槽
```cpp
// 连接信号槽
connect(sender, &Sender::signal, receiver, &Receiver::slot);

// 跨线程连接
connect(sender, &Sender::signal, receiver, &Receiver::slot, Qt::QueuedConnection);

// 发射信号
emit signalName();
```

### 线程
```cpp
// QtConcurrent 并行执行
QtConcurrent::run([this]() {
    // 耗时操作
});

// QThread
QThread *thread = new QThread();
Worker *worker = new Worker();
worker->moveToThread(thread);
thread->start();
```

### 定时器
```cpp
// 单次定时器
QTimer::singleShot(1000, this, &MainWindow::onTimeout);

// 循环定时器
QTimer *timer = new QTimer(this);
connect(timer, &QTimer::timeout, this, &MainWindow::onTimer);
timer->start(1000);
```

---

## 错误处理

### HALCON 异常
```cpp
try {
    HObject result;
    threshold(image, &result, 128, 255);
} catch (const HException &e) {
    qWarning() << "HALCON error:" << e.ErrorMessage();
    return false;
}
```

### Qt 错误处理
```cpp
// 检查文件是否存在
if (!QFile::exists(path)) {
    qWarning() << "File not found:" << path;
    return false;
}

// 检查目录是否存在
if (!QDir(dir).exists()) {
    QDir().mkpath(dir);
}
```

---

## 调试技巧

### 日志输出
```cpp
// 使用 Qt 日志
qDebug() << "Debug message";
qWarning() << "Warning message";
qCritical() << "Critical message";

// 使用项目日志宏
VFP_DEBUG << "Debug message";
VFP_EXEC_DEBUG << "Execution debug";
```

### 断点调试
```cpp
// 条件断点
if (condition) {
    __debugbreak();  // Windows
}

// 输出调试信息
qDebug() << "Variable:" << variable;
```

### 性能测量
```cpp
QElapsedTimer timer;
timer.start();

// 执行操作
processImage();

qDebug() << "Elapsed:" << timer.elapsed() << "ms";
```

---

## 常见问题快速解决

### 构建失败
```bash
# 清理构建目录
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### 找不到 HALCON
```powershell
$env:HALCON_ROOT = "D:\Program Files\MVTec\HALCON-24.11-Progress-Steady"
$env:HALCONARCH = "x64-win64"
```

### 找不到 OpenCV
```bash
cmake -B build -DOpenCV_DIR="E:/halcon/2/xin1/thirdparty/opencv/build"
```

### 测试失败
```bash
ctest --output-on-failure --verbose
```

---

## Git 工作流

### 分支操作
```bash
# 创建功能分支
git checkout -b feature/my-feature

# 提交更改
git add .
git commit -m "feat: add my feature"

# 推送分支
git push origin feature/my-feature

# 创建 Pull Request
gh pr create --title "feat: add my feature" --body "Description"
```

### 常用命令
```bash
# 查看状态
git status

# 查看差异
git diff

# 查看日志
git log --oneline

# 暂存更改
git stash
git stash pop
```

---

## 环境变量

### Qt 环境
```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.10.0\msvc2019_64"
```

### HALCON 环境
```powershell
$env:HALCON_ROOT = "D:\Program Files\MVTec\HALCON-24.11-Progress-Steady"
$env:HALCONARCH = "x64-win64"
```

### OpenCV 环境
```powershell
$env:OpenCV_DIR = "E:\halcon\2\xin1\thirdparty\opencv\build"
```

---

## 快捷键

### Visual Studio
```
F5          启动调试
F9          切换断点
F10         逐过程
F11         逐语句
Ctrl+K,C    注释
Ctrl+K,U    取消注释
Ctrl+K,D    格式化文档
```

### Qt Creator
```
F5          启动调试
F9          切换断点
F10         逐过程
F11         逐语句
Ctrl+/      注释
Ctrl+I      自动缩进
```

### Cursor
```
Ctrl+Shift+P   命令面板
Ctrl+P         快速打开文件
Ctrl+Shift+F   全局搜索
Ctrl+`         打开终端
```

---

## 联系方式

### 文档资源
- 快速入门: docs/quick-start-guide.md
- API 文档: docs/api-reference.md
- 故障排除: docs/troubleshooting-guide.md
- 最佳实践: docs/development-best-practices.md

### 工具脚本
- 项目状态: tools/project-status.ps1
- 配置验证: tools/verify-config.ps1
- 健康检查: tools/health-check.ps1

### 获取帮助
- 查看文档: docs/ 目录
- 搜索问题: GitHub Issues
- 联系团队: team@example.com
