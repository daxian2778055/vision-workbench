# CI/CD 配置和使用指南

> ⚠️ 2026-10-18 就地更正（正文原文照抄保留）：本文是一份"从零搭建"的蓝图，正文描述的作业——
> 覆盖率（OpenCppCoverage/Codecov）、安全扫描（CodeQL）、性能测试、部署、通知（Slack/Email）、
> 分支保护里的 static-analysis/code-quality/security-scan 状态检查——在本仓库从未装配过；文中
> 的 ci-cd.yml 也已随提交 27dcf86 删除。现仓库实际存在的只有两份工作流：`.github/workflows/ci.yml`
> （构建 + ctest，自托管 runner，fork PR 跳过）与 `.github/workflows/repo-hygiene.yml`（仓库卫生
> 检查）。请把本文当作建议稿阅读，装配与否属拍板项。

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

#### 6. 发布包一致性校验（tag / manifest / exe md5）

##### dist 目录生命周期（清理与身份追索）

- 发版包按 tag 命名放在 `dist/VisionFlowPlatform-<tag>`（历史约定也有 `-<日期>` 形式）；
  无日期目录 `dist/VisionFlowPlatform` 是**开发构建**，不是发版件。
- 包被更新版取代且台账无引用时可以删除（2026-10-24 清理过 `-20261013` / `-20261017` 两枚）。
  **身份追索分两半，别混为一谈**：tag 在 git 里，永远可追到提交；**exe md5 不在 git 里**——
  `版本说明.txt` 位于 gitignore 的 `dist/` 下、从未入库，而且从 tag 重建出来的 exe **不保证与
  原件逐字节相同**（构建不可逐字节复现）。
- 因此 **md5 必须先登记再清理**：`docs/release-registry.json` 是它唯一受跟踪的落点，
  清包前在 `releases` 里加一行 `tag / commit / exe_md5 / exe_bytes`；
  `release_manifest_check.py` 在解析出 tag 时会核对这一行（缺行、提交不符、md5 不符、
  字节数缺失或类型不对都判红）。
  这是 2026-10-24 复核补的：此前那句“md5 都在 git 里”是**说法强于实现**，
  `rc-20261023.2` 的 md5 就是在清理时险些永久丢失（靠部署日志才补录回来）。
- `legacy_backfilled` 收已清理、当时未登记的两枚（`rc-20261013` / `rc-20261017`）：
  提交由 tag 追索；md5 与字节数为 **2026-10-23 复核时对清理前包内 exe 的直接计算**
  （包已清理、事后无法就地复核——来源只写这件必定成立的事实，不把它归给某份仓内读不到的文件）。
- 台账/文档引用具体包路径时，若该包已被清理，在下一轮登记里补一句“该包已于 <日期> 清理，
  身份见 tag 与 md5”——**路径会随时间失效，tag + md5 不会**（2026-10-24 复核：归档一
  第一百四十条仍指着已删的 `-20261017`，按路径去找会扑空）。
- 清理只动 `dist/`（gitignore 内），不进版本历史；发版 tag 永不删除。

Windows 侧的正式发版流程是：

```powershell
# 1) 在干净的 HEAD 上构建 Release。
#    版本哈希由 vfp_version_header 目标在**每次构建**时刷新（cmake/RefreshVersionHeader.cmake），
#    不需要手动 reconfigure；但只要构建与打包之间又落了新提交，哈希就会旧——
#    所以"改代码 → 打包"之前必须再构建一次，否则门禁会以 exe_not_built_from_head 判红。
cmake --build build --config Release
# 2) 打 tag（annotated）并推送
git tag -a rc-YYYYMMDD -m "rc-YYYYMMDD —— 上线候选（提交 <short>）"
git push origin rc-YYYYMMDD
# 3) 出"带日期"的发版包（装机只认它；无日期 dist/VisionFlowPlatform 是开发构建）
powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -OutDir dist\VisionFlowPlatform-rc-YYYYMMDD
# 4) 登记（必做，且在清理任何旧包之前）：把 tag / commit / exe md5 / 字节写进
#    docs/release-registry.json —— 包内 版本说明.txt 在 gitignore 的 dist/ 下，
#    这是 md5 唯一受跟踪的落点；重建出来的 exe 不保证与原件逐字节相同。
git add docs/release-registry.json && git commit -m "发版登记：rc-YYYYMMDD → <short> → <exe md5>"
# 5) 机器校验：HEAD 带 rc tag 时自动校验带日期包（tag/manifest/exe-md5/exe 内嵌版本/发版登记）
python tools\release_manifest_check.py --repo . --mode require
```

`tools/release_manifest_check.py` 的判据（任一不满足即 RED）：

| 判据 | 失败码 |
|------|--------|
| 包目录 / 包内 `版本说明.txt` 存在且键齐全 | `package_missing` / `manifest_missing` / `manifest_key_missing` |
| **HEAD 带 `rc-*` tag 时，必须存在对应的带日期包 `dist/VisionFlowPlatform-<tag>`**（装机只认带日期的包；无日期目录是开发构建） | `dated_package_missing` |
| 包内 exe 的 md5 与字节数与 manifest 一致 | `exe_md5_mismatch` / `exe_bytes_mismatch` |
| manifest 的提交 == tag 指向的提交（无 tag 时 = HEAD；短哈希须 ≥7 位且为前缀） | `commit_mismatch` / `commit_too_short` |
| manifest 的工作树未提交改动数 == 0 | `dirty_worktree` |
| manifest 的标签与要求的 `rc-*` tag 一致 | `manifest_tag_mismatch` / `manifest_tag_not_on_head` |
| **解析出 tag 时，`exe 内嵌 HEAD 版本哈希` 必须存在且 == `yes`**（= exe 确实是打包时 HEAD 编出来的；缺行或 `no` 都红——旧包没有这一行同样判红，它没证明自己的来源） | `exe_not_built_from_head` / `manifest_key_missing` |
| **解析出 tag 时，`docs/release-registry.json` 必须有该 tag 的登记行，且 commit/exe_md5/exe_bytes 与包一致**（md5 不在 git 里、重建不保证同字节 ⇒ 清包前必须先登记；`exe_bytes` 必须是整数，缺字段或写成字符串同样判红，不会静默跳过） | `registry_missing` / `md5_not_registered` / `registry_commit_mismatch` / `registry_md5_mismatch` / `registry_bytes_missing` / `registry_bytes_mismatch` |

> 校验目标：未指定 `--package` 时，只要解析出的 tag（HEAD 上的 `rc-*`，或 `--expect-tag`）**存在
> 对应的带日期包就校验它**（目录名 `VisionFlowPlatform-<tag>` 或历史约定 `VisionFlowPlatform-<日期>`，
> 全名优先）；HEAD 带 tag 但没有带日期包 → RED `dated_package_missing`；无 tag 才校验
> `dist/VisionFlowPlatform`（开发构建）。发版后 main 继续前进时的复核用
> `--expect-tag <旧tag>`，此时以 tag 指向的提交为锚，并优先校验该 tag 的带日期包。

在 CI（`tools/ci.ps1`）里它是一道独立闸（退出码 **16**），其前还有一道 **Version header single-source check**（退出码 **17**，`tools/version_header_check.py`）：

- **先跑门禁自测** `tools/selftest_release_manifest.py`（22 个夹具用例，分别把每个判据做绿/做红，
  含"发版后 main 继续前进、包仍应对应 tag"、"HEAD 带 tag 必须校验带日期包"、
  "有带日期包时 --expect-tag 优先校验它"、"exe 内嵌版本 = no / 缺行必红"、
  "解析出 tag 却没有带日期包必红"、"发版登记缺行 / md5 不符 / 提交不符 / 缺字节数必红"各条）；
  自测本身失败 = 闸不可信 = exit 16。
- 版本头闸守的是"`cmake/Version.h.in` 是唯一正文数据源"这条承诺，三条判据：
  **覆盖**（模板里每个 `@TOKEN@`——大小写都算——都必须被 `cmake/RefreshVersionHeader.cmake`
  替换）、**赋值**（刷新脚本产出里版本号/哈希/日期必须分别落在正确的字段且值等于已知输入：
  常量版本号 / `git rev-parse --short HEAD` / 形如 `YYYY-MM-DD` 且接近当天（±1 天仅容跨零点）；
  **不能**用"A 的产出值喂 B"，否则 A 内部把值替错位时等价恒成立——2026-10-24 复审臂3 正是
  这么骗过等价判据的）、**等价**（两个生成器对同一组已知输入产出逐字节相同）。
  自证臂三枚：注入未替换的 token、把版本与哈希的替换值写反、把日期位换成版本号，都必须判红；
  `--selftest` 与正式校验任一非零 = exit 17。
- 两处脚本顶部都设了 `[Console]::OutputEncoding = UTF8`（`ci.ps1` / `ci_host_run.ps1`）：
  否则 PowerShell 用 cp936 解 Python 闸写出的 UTF-8 中文判词，`ci-*.log` 里中文全是乱码
  （finding 码与退出码是 ASCII，结论不受影响，但给人看的那半句丢了）。
- 再跑正式校验，模式 `auto`：HEAD **没有** `rc-*` tag 时打印 `skipped=1 reason=no_rc_tag_on_head`
  并放行（`dist/` 被 gitignore，开发构建允许陈旧）；HEAD **带** `rc-*` tag 时同一套判据为硬门禁。
  永远不会静默跳过——日志里必有 `[RM-VERDICT]` 或明确的 `skipped` 原因。

> 背景：2026-10-23 复核发现发布件核对靠人眼；同轮实测出 `deploy.ps1` 会在写 manifest 前中止
> （未打 tag 的 HEAD 上 `describe --exact-match` 非零；受限 PowerShell 缺 `Get-FileHash`），
> 于是"包少了自识别件"可以无人察觉。两处脚本缺陷已修，并加本闸防回潮。

#### 7. 画布编辑锁接线校验（exit 18）

2026-10-24 现场反馈：工具**拖不进**画布；双击能进，但进去后节点不能动。根因两条都在
`MainWindow` 的接线（不是算法）：

| 缺陷 | 表现 | 修法 |
|---|---|---|
| `onExecutionError` 不像 Started/Stopped/Finished 那样重算画布锁 | 错误路径若没走到 `executionFinished`，画布永久锁定 | 槽末补 `updateEditLockForCurrentScene()` |
| 工具箱双击走 `MainWindow::addNode` 直接 `createNode`，绕过 `FlowScene::dropEvent` 的锁判据（`createNode` 本身不查锁） | 锁定期间"拖不进、双击能进"，进去后不可动 | `addNode` 里加 `isEditLocked()` 判据 + 状态栏可见提示 |

两道守卫（各钉一半）：

- **运行期** `EditLockReleaseTest`：流程因节点失败停止后，`allowsGraphEditing()` 必须为真，
  且按该判据重算后画布解锁、节点恢复 `ItemIsMovable`——执行器若停在 Running 即红。
- **接线** `tools/edit_lock_wiring_check.py`（CI 第 18 步，先跑自证臂）：四个执行槽的函数体
  必须各有 `updateEditLockForCurrentScene`，`addNode` 必须有 `isEditLocked`；删掉任一处即红
  ——运行期用例钉不住"某个槽忘了调"，这正是缺陷所在位置。

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
