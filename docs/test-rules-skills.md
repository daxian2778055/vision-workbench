# 测试新规则和 SKILLS 的验证脚本

## 测试目标

验证新创建的规则文件和 SKILLS 是否正确配置并可用。

## 测试项目

### 1. 规则文件测试

#### 测试 OpenCV 开发规范
```cpp
// 测试代码：应该遵循 OpenCV 开发规范
cv::Mat processImage(const cv::Mat &input) {
    // 应该使用 const 引用传递
    cv::Mat output;
    cv::GaussianBlur(input, output, cv::Size(5, 5), 0);
    return output;
}
```

#### 测试深度学习集成规范
```cpp
// 测试代码：应该遵循深度学习集成规范
bool classifyImage(const HObject &input, HTuple modelHandle) {
    try {
        HTuple dlInput;
        GenDlInputFromImage(input, &dlInput);
        // 应该有异常处理
        return true;
    } catch (const HException &e) {
        qWarning() << "Error:" << e.ErrorMessage();
        return false;
    }
}
```

#### 测试编写规范
```cpp
// 测试代码：应该遵循测试编写规范
void testThreshold() {
    // 应该使用 Qt Test 框架
    QCOMPARE(1 + 1, 2);
    QVERIFY(true);
}
```

### 2. SKILLS 测试

#### 测试 C++ 代码审查 SKILL
- 检查内存管理
- 检查线程安全
- 检查性能优化
- 检查错误处理

#### 测试 HALCON 算子开发 SKILL
- 检查算子开发流程
- 检查常用算子使用
- 检查性能优化
- 检查错误处理

#### 测试工业视觉系统 SKILL
- 检查实时性优化
- 检查可靠性设计
- 检查相机集成
- 检查通信管理

## 测试步骤

### 步骤 1：验证规则文件存在

```powershell
# 检查规则文件是否存在
Test-Path "e:\halcon\2\xin1\.cursor\rules\opencv-patterns.mdc"
Test-Path "e:\halcon\2\xin1\.cursor\rules\deep-learning.mdc"
Test-Path "e:\halcon\2\xin1\.cursor\rules\testing.mdc"
```

### 步骤 2：验证 SKILLS 文件存在

```powershell
# 检查 SKILLS 文件是否存在
Test-Path "C:\Users\Administrator\.cursor\skills\cpp-code-review\SKILL.md"
Test-Path "C:\Users\Administrator\.cursor\skills\halcon-node-development\SKILL.md"
Test-Path "C:\Users\Administrator\.cursor\skills\industrial-vision-system\SKILL.md"
```

### 步骤 3：验证配置文件存在

```powershell
# 检查 MCP 配置文件是否存在
Test-Path "e:\halcon\2\xin1\.cursor\mcp.json"
```

### 步骤 4：测试规则内容

```powershell
# 读取规则文件内容
Get-Content "e:\halcon\2\xin1\.cursor\rules\opencv-patterns.mdc" | Select-String "核心原则"
Get-Content "e:\halcon\2\xin1\.cursor\rules\deep-learning.mdc" | Select-String "核心原则"
Get-Content "e:\halcon\2\xin1\.cursor\rules\testing.mdc" | Select-String "核心原则"
```

### 步骤 5：测试 SKILLS 内容

```powershell
# 读取 SKILLS 文件内容
Get-Content "C:\Users\Administrator\.cursor\skills\cpp-code-review\SKILL.md" | Select-String "审查维度"
Get-Content "C:\Users\Administrator\.cursor\skills\halcon-node-development\SKILL.md" | Select-String "开发流程"
Get-Content "C:\Users\Administrator\.cursor\skills\industrial-vision-system\SKILL.md" | Select-String "核心原则"
```

## 预期结果

1. **规则文件**：所有 3 个规则文件应该存在且内容正确
2. **SKILLS 文件**：所有 3 个 SKILLS 文件应该存在且内容正确
3. **配置文件**：MCP 配置文件应该存在且格式正确
4. **内容验证**：规则和 SKILLS 的内容应该包含预期的关键词

## 测试报告模板

```
## 测试报告

### 测试时间
[测试日期和时间]

### 测试环境
- 操作系统：Windows 10
- 项目路径：E:\halcon\2\xin1
- Cursor 版本：[版本号]

### 测试结果

#### 规则文件测试
- [ ] OpenCV 开发规范：存在/不存在，内容正确/不正确
- [ ] 深度学习集成规范：存在/不存在，内容正确/不正确
- [ ] 测试编写规范：存在/不存在，内容正确/不正确

#### SKILLS 文件测试
- [ ] C++ 代码审查：存在/不存在，内容正确/不正确
- [ ] HALCON 算子开发：存在/不存在，内容正确/不正确
- [ ] 工业视觉系统：存在/不存在，内容正确/不正确

#### 配置文件测试
- [ ] MCP 配置文件：存在/不存在，格式正确/不正确

### 问题记录
[记录任何发现的问题]

### 建议
[提供改进建议]
```

## 自动化测试脚本

```powershell
# test-rules-skills.ps1

Write-Host "开始测试新规则和 SKILLS..." -ForegroundColor Green

# 测试规则文件
$rules = @(
    "e:\halcon\2\xin1\.cursor\rules\opencv-patterns.mdc",
    "e:\halcon\2\xin1\.cursor\rules\deep-learning.mdc",
    "e:\halcon\2\xin1\.cursor\rules\testing.mdc"
)

foreach ($rule in $rules) {
    if (Test-Path $rule) {
        Write-Host "✓ 规则文件存在: $rule" -ForegroundColor Green
    } else {
        Write-Host "✗ 规则文件不存在: $rule" -ForegroundColor Red
    }
}

# 测试 SKILLS 文件
$skills = @(
    "C:\Users\Administrator\.cursor\skills\cpp-code-review\SKILL.md",
    "C:\Users\Administrator\.cursor\skills\halcon-node-development\SKILL.md",
    "C:\Users\Administrator\.cursor\skills\industrial-vision-system\SKILL.md"
)

foreach ($skill in $skills) {
    if (Test-Path $skill) {
        Write-Host "✓ SKILL 文件存在: $skill" -ForegroundColor Green
    } else {
        Write-Host "✗ SKILL 文件不存在: $skill" -ForegroundColor Red
    }
}

# 测试配置文件
$config = "e:\halcon\2\xin1\.cursor\mcp.json"
if (Test-Path $config) {
    Write-Host "✓ 配置文件存在: $config" -ForegroundColor Green
} else {
    Write-Host "✗ 配置文件不存在: $config" -ForegroundColor Red
}

Write-Host "测试完成！" -ForegroundColor Green
```

## 下一步

1. 运行测试脚本验证所有文件
2. 检查测试报告中的问题
3. 修复发现的问题
4. 更新文档
