# CI/CD 配置和使用指南

## 概述

本文档描述了 VisionFlowPlatform 项目的 CI/CD（持续集成/持续部署）配置和使用方法。

## CI/CD 流程

### 流程图

```
代码提交 → 静态分析 → 代码质量检查 → 构建 → 测试 → 覆盖率 → 安全扫描 → 性能测试 → 部署 → 通知
```

### 作业说明

#### 1. 静态分析作业 (static-analysis)
- **目的**：检查代码中的潜在问题
- **工具**：cppcheck、clang-tidy
- **触发条件**：每次提交和 PR
- **输出**：静态分析报告

#### 2. 代码质量作业 (code-quality)
- **目的**：检查代码复杂度和重复代码
- **工具**：lizard、cpd
- **触发条件**：静态分析通过后
- **输出**：复杂度报告、重复代码报告

#### 3. 构建作业 (build)
- **目的**：编译项目
- **配置**：Debug 和 Release 两种配置
- **依赖**：Qt 6.10、HALCON 24.11、OpenCV 4.13.0
- **输出**：构建产物

#### 4. 测试作业 (test)
- **目的**：运行单元测试和集成测试
- **工具**：CTest
- **依赖**：构建产物
- **输出**：测试结果

#### 5. 代码覆盖率作业 (coverage)
- **目的**：测量代码覆盖率
- **工具**：OpenCppCoverage、Codecov
- **依赖**：构建产物
- **输出**：覆盖率报告

#### 6. 安全扫描作业 (security-scan)
- **目的**：检查安全漏洞
- **工具**：CodeQL
- **依赖**：静态分析通过后
- **输出**：安全扫描报告

#### 7. 性能测试作业 (performance-test)
- **目的**：测试性能指标
- **工具**：自定义性能测试
- **依赖**：构建产物
- **输出**：性能测试结果

#### 8. 部署作业 (deploy)
- **目的**：创建部署包
- **触发条件**：主分支推送
- **依赖**：所有检查通过
- **输出**：部署包

#### 9. 通知作业 (notify)
- **目的**：发送构建状态通知
- **工具**：Slack、Email
- **触发条件**：所有作业完成后
- **输出**：通知消息

## 配置步骤

### 1. 创建 GitHub 仓库

```bash
# 初始化 Git 仓库
git init
git remote add origin https://github.com/your-org/VisionFlowPlatform.git

# 创建 .gitignore
cat > .gitignore << EOF
# Build directories
build/
cmake-build-*/

# IDE files
.vscode/
.idea/
*.user

# Compiled files
*.exe
*.dll
*.obj
*.o

# Coverage files
*.gcda
*.gcno
*.gcov
coverage.xml

# Logs
*.log

# Temporary files
*.tmp
*.temp
EOF
```

### 2. 配置 GitHub Secrets

在 GitHub 仓库设置中添加以下 Secrets：

```
# Qt 安装
QT_VERSION: '6.10.0'
QT_ARCH: 'win64_msvc2019_64'

# HALCON 配置
HALCON_ROOT: 'D:/Program Files/MVTec/HALCON-24.11-Progress-Steady'
HALCONARCH: 'x64-win64'

# OpenCV 配置
OpenCV_DIR: 'thirdparty/opencv/build'

# 通知配置
SLACK_WEBHOOK: 'https://hooks.slack.com/services/xxx/yyy/zzz'
EMAIL_USERNAME: 'ci@example.com'
EMAIL_PASSWORD: 'your-email-password'

# Codecov 配置
CODECOV_TOKEN: 'your-codecov-token'
```

### 3. 配置 GitHub Actions

将 CI/CD 配置文件放在正确位置：

```
.github/
└── workflows/
    └── ci-cd.yml
```

### 4. 配置分支保护规则

在 GitHub 仓库设置中配置分支保护规则：

```
Branch: main
Protection rules:
- Require pull request reviews before merging
- Require status checks to pass before merging
  - static-analysis
  - code-quality
  - build
  - test
  - coverage
  - security-scan
- Require branches to be up to date before merging
- Require linear history
- Include administrators
```

## 使用指南

### 1. 提交代码

```bash
# 创建功能分支
git checkout -b feature/new-feature

# 添加文件
git add .

# 提交更改
git commit -m "feat: add new feature"

# 推送到远程
git push origin feature/new-feature
```

### 2. 创建 Pull Request

1. 在 GitHub 上创建 Pull Request
2. 填写 PR 描述
3. 选择审查者
4. 等待 CI/CD 检查通过

### 3. 监控构建状态

#### 查看构建状态
- 在 GitHub 仓库页面查看 Actions 标签
- 点击具体的构建查看详细日志
- 查看各个作业的执行状态

#### 查看构建报告
- 静态分析报告：下载 cppcheck-report 和 clang-tidy-report
- 代码质量报告：下载 quality-reports
- 测试结果：下载 test-results
- 覆盖率报告：访问 Codecov 查看

### 4. 处理构建失败

#### 静态分析失败
```bash
# 本地运行 cppcheck
cppcheck --enable=all --suppress=missingInclude src/ include/

# 本地运行 clang-tidy
clang-tidy -p build src/*.cpp

# 修复问题后重新提交
```

#### 代码质量检查失败
```bash
# 本地运行复杂度分析
lizard src/*.cpp

# 本地运行重复代码检测
cpd --minimum-tokens 50 --language cpp --dir src/

# 重构代码后重新提交
```

#### 构建失败
```bash
# 本地构建
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 修复编译错误后重新提交
```

#### 测试失败
```bash
# 本地运行测试
cd build
ctest --output-on-failure

# 修复测试后重新提交
```

### 5. 部署流程

#### 自动部署
当代码推送到 `main` 分支时，会自动触发部署流程：

1. 所有 CI/CD 检查通过
2. 创建部署包
3. 上传部署包为 GitHub Artifact

#### 手动部署
```bash
# 下载部署包
# 从 GitHub Artifacts 下载 deployment-package

# 解压部署包
mkdir deploy
cd deploy
unzip ../deployment-package.zip

# 运行部署脚本
./deploy.sh
```

## 配置文件详解

### 1. GitHub Actions 配置 (.github/workflows/ci-cd.yml)

```yaml
# 触发条件
on:
  push:
    branches: [ main, develop ]  # 主要分支推送
  pull_request:
    branches: [ main, develop ]  # PR 创建和更新

# 环境变量
env:
  HALCON_ROOT: 'D:/Program Files/MVTec/HALCON-24.11-Progress-Steady'
  HALCONARCH: 'x64-win64'
  OpenCV_DIR: 'thirdparty/opencv/build'

# 作业定义
jobs:
  static-analysis:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install tools
        run: |
          choco install cppcheck -y
          choco install llvm -y
      - name: Run cppcheck
        run: cppcheck --enable=all src/ include/
      - name: Run clang-tidy
        run: clang-tidy -p build src/*.cpp
```

### 2. 本地构建脚本 (tools/build-local.ps1)

```powershell
# 本地构建脚本
param(
    [string]$BuildType = "Release",
    [switch]$RunTests,
    [switch]$GenerateCoverage
)

Write-Host "Building VisionFlowPlatform..." -ForegroundColor Green

# 生成 compile_commands.json
cmake -B build -DCMAKE_BUILD_TYPE=$BuildType -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 构建项目
cmake --build build --config $BuildType --parallel

if ($RunTests) {
    Write-Host "Running tests..." -ForegroundColor Yellow
    cd build
    ctest --output-on-failure -C $BuildType
    cd ..
}

if ($GenerateCoverage) {
    Write-Host "Generating coverage..." -ForegroundColor Yellow
    # 生成覆盖率报告
}

Write-Host "Build complete!" -ForegroundColor Green
```

### 3. 本地测试脚本 (tools/test-local.ps1)

```powershell
# 本地测试脚本
param(
    [string]$TestType = "all",
    [switch]$Verbose
)

Write-Host "Running tests..." -ForegroundColor Green

switch ($TestType) {
    "unit" {
        # 运行单元测试
        cd build
        ctest --output-on-failure -R "unit"
        cd ..
    }
    "integration" {
        # 运行集成测试
        cd build
        ctest --output-on-failure -R "integration"
        cd ..
    }
    "performance" {
        # 运行性能测试
        .\build\bin\VisionFlowPlatform.exe --performance-test
    }
    default {
        # 运行所有测试
        cd build
        ctest --output-on-failure
        cd ..
    }
}

Write-Host "Tests complete!" -ForegroundColor Green
```

## 监控和报告

### 1. 构建状态监控

#### GitHub Actions 仪表板
- 访问仓库的 Actions 标签
- 查看构建历史和状态
- 查看作业执行时间

#### 构建状态徽章
在 README.md 中添加构建状态徽章：

```markdown
![CI/CD Status](https://github.com/your-org/VisionFlowPlatform/actions/workflows/ci-cd.yml/badge.svg)
```

### 2. 代码质量报告

#### Codecov 仪表板
- 访问 [codecov.io](https://codecov.io)
- 查看代码覆盖率趋势
- 查看覆盖率详情

#### 静态分析报告
- 下载 cppcheck-report.xml
- 使用 cppcheck-htmlreport 生成 HTML 报告
- 分析问题并修复

### 3. 性能监控

#### 性能测试结果
- 下载 performance-results.json
- 分析性能指标
- 识别性能瓶颈

#### 趋势分析
- 比较不同版本的性能
- 识别性能退化
- 优化关键路径

## 故障排除

### 1. 常见问题

#### 问题：Qt 安装失败
**解决方案**：
```yaml
- name: Install Qt
  uses: jurplel/install-qt-action@v3
  with:
    version: '6.10.0'
    arch: win64_msvc2019_64
    cached: 'false'
```

#### 问题：HALCON 找不到
**解决方案**：
```yaml
- name: Setup HALCON
  run: |
    echo "HALCON_ROOT=D:/Program Files/MVTec/HALCON-24.11-Progress-Steady" >> $GITHUB_ENV
```

#### 问题：编译错误
**解决方案**：
1. 检查代码语法错误
2. 检查依赖库版本
3. 检查编译器版本

#### 问题：测试失败
**解决方案**：
1. 检查测试代码
2. 检查测试数据
3. 检查测试环境

### 2. 调试技巧

#### 查看详细日志
在 GitHub Actions 中启用调试日志：
```yaml
env:
  ACTIONS_STEP_DEBUG: true
```

#### 本地重现问题
```bash
# 本地运行 CI/CD 流程
act -j static-analysis
act -j build
act -j test
```

#### 检查构建环境
```bash
# 检查构建环境
echo "OS: $RUNNER_OS"
echo "Qt version: $QT_VERSION"
echo "HALCON root: $HALCON_ROOT"
```

## 最佳实践

### 1. 提交规范

#### 提交信息格式
```
<type>(<scope>): <subject>

<body>

<footer>
```

**类型**：
- `feat`: 新功能
- `fix`: 修复 bug
- `docs`: 文档更新
- `style`: 代码格式调整
- `refactor`: 代码重构
- `test`: 测试相关
- `chore`: 构建/工具相关

**示例**：
```
feat(node): add new blob analysis node

- Add BlobAnalysisNode class
- Implement blob detection algorithm
- Add unit tests

Closes #123
```

### 2. 分支策略

#### Git Flow
- `main`: 生产分支
- `develop`: 开发分支
- `feature/*`: 功能分支
- `release/*`: 发布分支
- `hotfix/*`: 热修复分支

#### 分支命名
```
feature/add-new-node
fix/memory-leak-in-flow-executor
docs/update-api-documentation
refactor/optimize-image-processing
test/add-unit-tests-for-threshold
```

### 3. 代码审查

#### 审查清单
- [ ] 代码符合项目规范
- [ ] 无静态分析警告
- [ ] 测试覆盖率足够
- [ ] 文档已更新
- [ ] 性能无退化

#### 审查流程
1. 创建 Pull Request
2. 自动运行 CI/CD 检查
3. 同行审查代码
4. 修复审查意见
5. 合并到主分支

### 4. 发布流程

#### 版本号规范
- 主版本号：重大变更
- 次版本号：新功能
- 修订号：bug 修复

#### 发布步骤
1. 创建 release 分支
2. 更新版本号
3. 运行完整测试
4. 创建 GitHub Release
5. 生成发布包
6. 部署到生产环境

## 扩展和定制

### 1. 添加新的检查

#### 添加新的静态分析工具
```yaml
- name: Run new-tool
  run: |
    # 安装新工具
    choco install new-tool -y
    
    # 运行检查
    new-tool --check src/
```

#### 添加新的测试类型
```yaml
- name: Run new-test
  run: |
    # 运行新测试
    .\build\bin\VisionFlowPlatform.exe --new-test
```

### 2. 自定义通知

#### 添加 Slack 通知
```yaml
- name: Send Slack notification
  uses: 8398a7/action-slack@v3
  with:
    status: ${{ job.status }}
    channel: '#build-notifications'
    webhook_url: ${{ secrets.SLACK_WEBHOOK }}
```

#### 添加邮件通知
```yaml
- name: Send email notification
  uses: dawidd6/action-send-mail@v3
  with:
    server_address: smtp.gmail.com
    server_port: 465
    username: ${{ secrets.EMAIL_USERNAME }}
    password: ${{ secrets.EMAIL_PASSWORD }}
    subject: "Build ${{ job.status }}"
    body: "Build completed with status: ${{ job.status }}"
    to: team@example.com
    from: ci@example.com
```

### 3. 集成其他服务

#### 集成 SonarQube
```yaml
- name: SonarQube scan
  uses: sonarsource/sonarqube-scan-action@master
  env:
    SONAR_TOKEN: ${{ secrets.SONAR_TOKEN }}
    SONAR_HOST_URL: ${{ secrets.SONAR_HOST_URL }}
```

#### 集成 Coverity
```yaml
- name: Coverity scan
  uses: coverityapp/coverity-scan-action@v1
  with:
    project: 'VisionFlowPlatform'
    token: ${{ secrets.COVERITY_TOKEN }}
```

## 总结

通过配置和使用 CI/CD 流程，可以实现：

1. **自动化检查**：静态分析、代码质量、安全扫描
2. **自动化构建**：在不同平台上自动构建
3. **自动化测试**：单元测试、集成测试、性能测试
4. **自动化部署**：自动创建部署包
5. **自动化通知**：构建状态通知

这些自动化流程将帮助团队：
- 提高代码质量
- 减少手动错误
- 加快开发周期
- 提高团队协作效率

通过持续监控和改进 CI/CD 流程，可以进一步优化开发流程和代码质量。
