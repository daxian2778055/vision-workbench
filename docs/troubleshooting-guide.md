# VisionFlowPlatform 故障排除指南

## 概述

本指南帮助您解决 VisionFlowPlatform 项目开发和运行中遇到的常见问题。

---

## 构建问题

### 问题 1：CMake 配置失败

**症状**：
```
CMake Error at CMakeLists.txt:xx (find_package):
  Could not find a package configuration file provided by "Qt6"
```

**解决方案**：

```bash
# 1. 检查 Qt 是否安装
ls "C:/Qt/6.10.0/msvc2019_64"

# 2. 设置 Qt 路径
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.10.0/msvc2019_64"

# 3. 或者设置环境变量
export CMAKE_PREFIX_PATH="C:/Qt/6.10.0/msvc2019_64"
cmake -B build
```

### 问题 2：找不到 HALCON

**症状**：
```
CMake Error: Could not find HALCON
```

**解决方案**：

```bash
# 1. 检查 HALCON 安装路径
ls "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady"

# 2. 设置 HALCON_ROOT
cmake -B build -DHALCON_ROOT="D:/Program Files/MVTec/HALCON-24.11-Progress-Steady"

# 3. 或者设置环境变量
export HALCON_ROOT="D:/Program Files/MVTec/HALCON-24.11-Progress-Steady"
export HALCONARCH="x64-win64"
cmake -B build
```

### 问题 3：找不到 OpenCV

**症状**：
```
CMake Error: Could not find OpenCV
```

**解决方案**：

```bash
# 1. 检查 OpenCV 路径
ls thirdparty/opencv/build

# 2. 设置 OpenCV_DIR
cmake -B build -DOpenCV_DIR="E:/halcon/2/xin1/thirdparty/opencv/build"

# 3. 或者在 CMakeLists.txt 中设置
set(OpenCV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opencv/build")
```

### 问题 4：编译错误 - 找不到头文件

**症状**：
```
fatal error: 'HalconCpp.h': No such file or directory
```

**解决方案**：

```bash
# 1. 检查 HALCON 头文件路径
ls "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady/include"

# 2. 确保 CMakeLists.txt 中包含正确路径
include_directories(
    ${HALCON_ROOT}/include
    ${HALCON_ROOT}/include/halconcpp
)

# 3. 重新生成构建文件
rm -rf build
cmake -B build
```

### 问题 5：链接错误 - 找不到库文件

**症状**：
```
LINK : fatal error LNK1181: cannot open input file 'halconcpp.lib'
```

**解决方案**：

```bash
# 1. 检查库文件路径
ls "D:/Program Files/MVTec/HALCON-24.11-Progress-Steady/lib/x64-win64"

# 2. 确保 CMakeLists.txt 中链接正确库
target_link_libraries(VisionFlowPlatform
    ${HALCON_ROOT}/lib/x64-win64/halconcpp.lib
    ${HALCON_ROOT}/lib/x64-win64/halcon.lib
)

# 3. 重新构建
cmake --build build --config Release
```

### 问题 6：MSVC 版本不匹配

**症状**：
```
CMake Error: The C++ compiler "cl.exe" is not able to compile a simple test program
```

**解决方案**：

```bash
# 1. 检查 Visual Studio 版本
# 确保安装了 VS2019 或 VS2022

# 2. 使用正确的生成器
cmake -B build -G "Visual Studio 16 2019" -A x64
# 或者
cmake -B build -G "Visual Studio 17 2022" -A x64

# 3. 确保在正确的环境中运行
# 使用 "Developer Command Prompt for VS"
```

---

## 运行时问题

### 问题 1：程序启动崩溃

**症状**：
```
应用程序无法正常启动 (0xc000007b)
```

**解决方案**：

```powershell
# 1. 检查依赖 DLL
# 确保以下 DLL 在 PATH 中或程序目录中：
# - Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll 等
# - halcon.dll, halconcpp.dll
# - opencv_world4130.dll
# - MvCameraControl.dll

# 2. 设置 PATH 环境变量
$env:PATH += ";C:\Qt\6.10.0\msvc2019_64\bin"
$env:PATH += ";D:\Program Files\MVTec\HALCON-24.11-Progress-Steady\bin\x64-win64"
$env:PATH += ";E:\halcon\2\xin1\thirdparty\opencv\build\x64\vc16\bin"

# 3. 运行程序
.\build\bin\VisionFlowPlatform.exe
```

### 问题 2：找不到相机

**症状**：
```
No camera found
```

**解决方案**：

```powershell
# 1. 检查相机连接
# 确保相机已连接并开启

# 2. 检查 MVS 驱动
# 确保安装了海康 MVS SDK
ls "D:/MVS"

# 3. 检查相机枚举
# 运行 MVS 工具查看相机列表

# 4. 检查权限
# 确保有访问相机的权限
```

### 问题 3：图像显示异常

**症状**：
```
图像显示为黑色或乱码
```

**解决方案**：

```cpp
// 1. 检查图像格式
HObject image;
read_image(&image, "test.png");

HTuple width, height, channels;
GetImageSize(image, &width, &height);
CountChannels(image, &channels);

qDebug() << "Size:" << width.I() << "x" << height.I();
qDebug() << "Channels:" << channels.I();

// 2. 检查显示窗口
HalconWindow *window = getHalconWindow();
if (!window) {
    qWarning() << "Window not initialized";
    return;
}

// 3. 检查图像数据
if (!image.IsInitialized()) {
    qWarning() << "Image not initialized";
    return;
}
```

### 问题 4：内存泄漏

**症状**：
```
程序内存持续增长
```

**解决方案**：

```cpp
// 1. 使用智能指针
auto data = std::make_unique<DataObject>();
// 而不是
DataObject *data = new DataObject();

// 2. 检查 HALCON 对象
HObject image;
// 确保在不需要时释放
image.Clear();

// 3. 检查 Qt 对象
// 确保设置了父对象
QWidget *widget = new QWidget(parent);

// 4. 使用内存检测工具
// Valgrind (Linux)
// Dr. Memory (Windows)
// Application Verifier (Windows)
```

### 问题 5：线程安全问题

**症状**：
```
随机崩溃
数据竞争
UI 卡顿
```

**解决方案**：

```cpp
// 1. 使用正确的信号槽连接
connect(sender, &Sender::signal,
        receiver, &Receiver::slot,
        Qt::QueuedConnection);  // 跨线程使用 QueuedConnection

// 2. 保护共享数据
QMutex mutex;
QMutexLocker locker(&mutex);
// 访问共享数据

// 3. 确保 UI 操作在主线程
if (QThread::currentThread() != qApp->thread()) {
    QMetaObject::invokeMethod(this, [this]() {
        // UI 操作
    }, Qt::QueuedConnection);
    return;
}

// 4. 使用线程安全的数据结构
QQueue<QSharedPointer<DataObject>> dataQueue;
QMutex queueMutex;
```

---

## 工具问题

### 问题 1：mcp-cpp 无法启动

**症状**：
```
mcp-cpp-server: command not found
```

**解决方案**：

```powershell
# 1. 检查 Rust 安装
rustc --version
cargo --version

# 2. 检查 mcp-cpp-server 安装
cargo install mcp-cpp-server

# 3. 检查 PATH
$env:PATH += ";$env:USERPROFILE\.cargo\bin"

# 4. 重启终端
```

### 问题 2：clang-tidy 找不到

**症状**：
```
clang-tidy: command not found
```

**解决方案**：

```powershell
# 1. 检查 LLVM 安装
choco install llvm

# 2. 检查 PATH
$env:PATH += ";C:\Program Files\LLVM\bin"

# 3. 或者指定完整路径
clang-tidy --version

# 4. 重启终端
```

### 问题 3：cppcheck 找不到

**症状**：
```
cppcheck: command not found
```

**解决方案**：

```powershell
# 1. 检查 cppcheck 安装
choco install cppcheck

# 2. 检查 PATH
$env:PATH += ";C:\Program Files\Cppcheck"

# 3. 或者指定完整路径
cppcheck --version

# 4. 重启终端
```

### 问题 4：compile_commands.json 不存在

**症状**：
```
clang-tidy: error: no compilation database found
```

**解决方案**：

```bash
# 1. 生成 compile_commands.json
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 2. 检查文件是否存在
ls build/compile_commands.json

# 3. 使用正确的路径
clang-tidy -p build src/*.cpp
```

---

## 测试问题

### 问题 1：测试失败

**症状**：
```
Test failed: ThresholdNodeTest::testProcess
```

**解决方案**：

```bash
# 1. 查看详细输出
ctest --output-on-failure

# 2. 运行特定测试
ctest -R ThresholdNodeTest --verbose

# 3. 检查测试代码
# 确保测试数据存在
# 确保测试环境正确

# 4. 调试测试
# 在测试代码中添加断点
# 使用调试器运行测试
```

### 问题 2：测试覆盖率低

**症状**：
```
Code coverage: 45%
```

**解决方案**：

```bash
# 1. 生成覆盖率报告
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
cmake --build build
ctest
gcov src/*.cpp
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report

# 2. 查看未覆盖的代码
# 打开 coverage_report/index.html

# 3. 添加缺失的测试
# 为未覆盖的代码路径添加测试
```

### 问题 3：测试超时

**症状**：
```
Test timeout: FlowExecutionTest::testLargeFlow
```

**解决方案**：

```cpp
// 1. 增加超时时间
void FlowExecutionTest::testLargeFlow() {
    QSignalSpy finishedSpy(&executor, &FlowExecutor::executionFinished);
    executor.startExecution();
    QVERIFY(finishedSpy.wait(30000));  // 30 秒超时
}

// 2. 优化测试
// 减少测试数据量
// 使用 mock 对象
// 并行执行测试

// 3. 检查性能瓶颈
// 使用性能分析工具
// 优化被测代码
```

---

## CI/CD 问题

### 问题 1：GitHub Actions 构建失败

**症状**：
```
Job failed: build
```

**解决方案**：

```yaml
# 1. 检查依赖安装
- name: Install dependencies
  run: |
    choco install qt6 -y
    choco install llvm -y
    choco install cppcheck -y

# 2. 检查环境变量
env:
  HALCON_ROOT: 'D:/Program Files/MVTec/HALCON-24.11-Progress-Steady'
  OpenCV_DIR: 'thirdparty/opencv/build'

# 3. 查看详细日志
# 在 GitHub Actions 页面查看详细日志

# 4. 本地重现问题
act -j build
```

### 问题 2：静态分析误报

**症状**：
```
cppcheck: warning: Possible null pointer dereference
```

**解决方案**：

```bash
# 1. 抑制特定警告
cppcheck --suppress=nullPointer src/MainWindow.cpp

# 2. 使用内联抑制
// cppcheck-suppress nullPointer
*ptr = value;

# 3. 配置抑制文件
# 创建 suppressions.xml
<suppressions>
    <suppress>
        <id>nullPointer</id>
        <fileName>src/MainWindow.cpp</fileName>
        <lineNumber>123</lineNumber>
    </suppress>
</suppressions>

cppcheck --suppressions-list=suppressions.xml src/
```

### 问题 3：部署失败

**症状**：
```
Deploy failed: artifact not found
```

**解决方案**：

```yaml
# 1. 检查构建是否成功
- name: Check build status
  run: |
    if [ ! -f "build/bin/VisionFlowPlatform.exe" ]; then
      echo "Build failed!"
      exit 1
    fi

# 2. 检查 artifact 上传
- name: Upload artifacts
  uses: actions/upload-artifact@v3
  with:
    name: build-artifacts
    path: build/bin/*.exe

# 3. 检查部署步骤
- name: Deploy
  run: |
    # 确保 artifacts 存在
    ls artifacts/
```

---

## 性能问题

### 问题 1：程序运行缓慢

**症状**：
```
程序响应慢
图像处理延迟高
```

**解决方案**：

```cpp
// 1. 使用性能分析
QElapsedTimer timer;
timer.start();

// 执行操作
processImage();

qDebug() << "Elapsed:" << timer.elapsed() << "ms";

// 2. 优化算法
// 使用更快的算法
// 减少不必要的计算
// 使用缓存

// 3. 并行处理
QtConcurrent::map(images, [](HObject &image) {
    processImage(image);
});

// 4. 使用 GPU 加速
// HALCON GPU 加速
// OpenCL/CUDA
```

### 问题 2：内存使用过高

**症状**：
```
内存使用持续增长
程序崩溃
```

**解决方案**：

```cpp
// 1. 检查内存泄漏
// 使用内存检测工具

// 2. 优化内存使用
// 使用图像缓存池
// 及时释放不需要的资源
// 使用智能指针

// 3. 减少数据拷贝
// 使用引用传递
// 使用移动语义
void process(const HObject &image);  // 引用传递
HObject result = std::move(tempImage);  // 移动语义

// 4. 使用内存池
class ImagePool {
    static HObject acquire(int width, int height);
    static void release(const HObject &image);
};
```

### 问题 3：UI 卡顿

**症状**：
```
UI 响应慢
界面冻结
```

**解决方案**：

```cpp
// 1. 避免在主线程执行耗时操作
void MainWindow::startProcessing() {
    // 在后台线程执行
    QtConcurrent::run([this]() {
        processImage();
        
        // 更新 UI（在主线程）
        QMetaObject::invokeMethod(this, [this]() {
            updateDisplay();
        });
    });
}

// 2. 使用进度条
void MainWindow::updateProgress(int value) {
    ui->progressBar->setValue(value);
    QCoreApplication::processEvents();  // 处理事件
}

// 3. 使用定时器
QTimer *timer = new QTimer(this);
connect(timer, &QTimer::timeout, this, &MainWindow::update);
timer->start(100);  // 每 100ms 更新一次
```

---

## 通信问题

### 问题 1：TCP 连接失败

**症状**：
```
Connection refused
Timeout
```

**解决方案**：

```cpp
// 1. 检查网络连接
QTcpSocket socket;
socket.connectToHost("192.168.1.100", 502);

if (!socket.waitForConnected(3000)) {
    qWarning() << "Connection failed:" << socket.errorString();
    return;
}

// 2. 检查防火墙
// 确保端口未被阻止

// 3. 检查服务器状态
// 确保服务器正在运行

// 4. 增加超时时间
socket.waitForConnected(10000);  // 10 秒超时
```

### 问题 2：串口通信失败

**症状**：
```
Port not available
Read timeout
```

**解决方案**：

```cpp
// 1. 检查串口
QSerialPort serial;
serial.setPortName("COM1");

if (!serial.open(QIODevice::ReadWrite)) {
    qWarning() << "Open port failed:" << serial.errorString();
    return;
}

// 2. 配置串口参数
serial.setBaudRate(QSerialPort::Baud115200);
serial.setDataBits(QSerialPort::Data8);
serial.setParity(QSerialPort::NoParity);
serial.setStopBits(QSerialPort::OneStop);

// 3. 检查数据格式
// 确保发送和接收的数据格式一致

// 4. 增加超时时间
serial.waitForReadyRead(1000);  // 1 秒超时
```

### 问题 3：Modbus 通信失败

**症状**：
```
Modbus error: Invalid response
CRC error
```

**解决方案**：

```cpp
// 1. 检查 Modbus 配置
ModbusClient client;
client.setHost("192.168.1.100");
client.setPort(502);

// 2. 检查寄存器地址
// 确保寄存器地址正确

// 3. 检查数据类型
// 确保数据类型匹配

// 4. 检查 CRC 校验
// 确保 CRC 计算正确
```

---

## 调试技巧

### 1. 使用日志

```cpp
// 启用详细日志
qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss.zzz} "
                   "%{if-debug}DEBUG%{endif}"
                   "%{if-info}INFO%{endif}"
                   "%{if-warning}WARN%{endif}"
                   "%{if-critical}ERROR%{endif}"
                   " [%{category}] %{message}");

// 使用日志类别
Q_LOGGING_CATEGORY(vfpCat, "VisionFlowPlatform")

qCDebug(vfpCat) << "Debug message";
qCInfo(vfpCat) << "Info message";
qCWarning(vfpCat) << "Warning message";
qCCritical(vfpCat) << "Critical message";
```

### 2. 使用断点

```cpp
// 条件断点
if (condition) {
    __debugbreak();  // Windows
    // raise(SIGTRAP);  // Linux
}

// 数据断点
// 在调试器中设置数据断点
```

### 3. 使用调试器

```bash
# Visual Studio
# 1. 打开 build/VisionFlowPlatform.sln
# 2. 设置断点
# 3. 按 F5 启动调试

# Qt Creator
# 1. 打开 CMakeLists.txt
# 2. 配置构建
# 3. 设置断点
# 4. 按 F5 启动调试

# GDB (Linux)
gdb ./build/bin/VisionFlowPlatform
(gdb) break main
(gdb) run
(gdb) next
(gdb) print variable
```

### 4. 使用性能分析

```bash
# Windows
# 使用 Visual Studio Profiler
# 使用 Intel VTune
# 使用 AMD CodeXL

# Linux
# 使用 perf
perf record -g ./build/bin/VisionFlowPlatform
perf report

# 使用 Valgrind
valgrind --tool=callgrind ./build/bin/VisionFlowPlatform
```

---

## 获取帮助

### 1. 查看日志

```bash
# 查看应用程序日志
cat logs/app.log

# 查看系统日志
# Windows: Event Viewer
# Linux: /var/log/syslog
```

### 2. 搜索问题

```bash
# 在 GitHub 上搜索
# https://github.com/your-org/VisionFlowPlatform/issues

# 在 Stack Overflow 上搜索
# https://stackoverflow.com/questions/tagged/visionflowplatform

# 在 Qt 论坛上搜索
# https://forum.qt.io/

# 在 HALCON 论坛上搜索
# https://www.mvtec.com/company/contact/
```

### 3. 联系团队

- **邮件**: team@example.com
- **Slack**: #visionflowplatform
- **GitHub Issues**: [创建 Issue](https://github.com/your-org/VisionFlowPlatform/issues/new)

### 4. 提交 Bug 报告

```markdown
## Bug 报告

### 环境信息
- 操作系统: Windows 10/11
- Qt 版本: 6.10.0
- HALCON 版本: 24.11
- OpenCV 版本: 4.13.0
- 编译器: MSVC 2019/2022

### 问题描述
[清晰描述问题]

### 复现步骤
1. 步骤 1
2. 步骤 2
3. 步骤 3

### 预期行为
[描述预期行为]

### 实际行为
[描述实际行为]

### 日志/截图
[附上相关日志或截图]

### 其他信息
[任何其他相关信息]
```

---

## 预防措施

### 1. 定期备份

```bash
# 使用 Git 管理代码
git add .
git commit -m "backup: save progress"
git push origin main

# 定期创建备份
cp -r project project_backup_$(date +%Y%m%d)
```

### 2. 代码审查

- 提交前运行静态分析
- 进行同行审查
- 运行所有测试

### 3. 持续集成

- 使用 CI/CD 自动化构建和测试
- 定期运行完整测试套件
- 监控代码质量

### 4. 文档更新

- 及时更新文档
- 记录已知问题
- 分享解决方案

---

## 总结

本故障排除指南涵盖了 VisionFlowPlatform 项目中常见的问题和解决方案。如果遇到本指南未涵盖的问题，请：

1. **查看日志**：获取详细错误信息
2. **搜索问题**：查找类似问题的解决方案
3. **联系团队**：寻求团队帮助
4. **提交 Issue**：报告问题并跟踪解决进度

通过系统性的故障排除和预防措施，可以大大减少开发和运行中的问题。
