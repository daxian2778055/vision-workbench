# VisionFlowPlatform 国际化支持指南

## 概述

本指南描述了 VisionFlowPlatform 项目的国际化（i18n）和本地化（l10n）支持，帮助开发者添加多语言支持。

> ⚠️ **现状声明（2026-09-27 新增，请先读这一段再往下翻）**：下面通篇是**通用 Qt 国际化教程**，**不是本仓现状**——它连文件名都和本仓不一样。逐条对账（每条都给复现命令）：
>
> | 本文写法 | 仓内实况 | 复现命令 |
> |---|---|---|
> | `visionflow_zh_CN.ts/.qm`、`visionflow_en_US.ts/.qm`（§1.2 项目结构、§3.1、§7.3 等，共 **27 行**） | `translations/` 实际只有 **`visionflow_en.ts`／`visionflow_en.qm`／`apply_en_translations.py`** 三份；**没有中文 `.ts/.qm`**——源文本身就是简体中文（`include/I18n.h:2`）。注意 `src/I18n.cpp:31` 确实还留着 `"visionflow_zh_CN"` 这个串，但它是**死值**：`installTranslators` 在 `:43`～`:45` 对中文档直接 `return true`，从不去 load ⇒ 教程那两套中文文件名在本仓**从来没有对应的加载路径** | `ls translations`／读 `src/I18n.cpp:40` |
> | 语言代码 `en_US`、以及 `ja_JP`／`ko_KR`／`de_DE`／`fr_FR`（§4.2、§4.3、§9.1「计划支持的语言」） | 仓内只有 **`"zh_CN"` 与 `"en"`** 两个取值（`src/I18n.cpp:15`／`:24`），英文 `.qm` 基名 `visionflow_en`（`src/I18n.cpp:30`）；`I18n::displayName` 也只返回「English／简体中文」（`:34`～`:36`），语言菜单同样只挂两项（`src/MainWindow.cpp:1097`～`:1102`）⇒ §9.1 那张表**从未成立过**（日／韩／德／法只是教程里的占位示例） | `grep -n zh_CN src/I18n.cpp` |
> | `find_package(Qt6 ... LinguistTools)` ＋ `qt_add_translations(...)`（§3.1） | 仓里**没用** `qt_add_translations`；实际是直接调 `lrelease` 可执行文件的手写块，`CMakeLists.txt:596` 那句注释写明了原因：「避免依赖可能未随 Qt6 主包发布的 LinguistTools CMake 组件」 | `grep -n qt_add_translations CMakeLists.txt`（**0 命中**）／看 `CMakeLists.txt:593`～`:628` |
> | 切换语言即时生效（§4.2 的 `emit languageChanged(locale)`） | **重启生效**：`src/MainWindow.cpp:2149`「语言设置将在重启后生效」，同处注释说明 Qt Widgets 大多在构造期取译文。装载点是 `src/I18n.cpp:40 installTranslators`，由 `src/main.cpp:308` 在 `MainWindow` 构造前调用 | 读那两个行号 |
> | 通篇假设「包了 `tr()` 就翻译了」 | 已包 **219 处**（只集中在 **6 份**文件，`src/MainWindow.cpp` 一份占 179）；仓内**裸中文字面量 2325 条**（作用域 403 份文件，且是**下界**——另 **4127 处 `\uXXXX` 转义中文**对扫描器和 `lupdate` 都是 ASCII）⇒ 英文档下大部分界面仍是中文 | `python tools/scan_i18n_surface.py` |
>
> **并且：英文界面交付已于 2026-09-27 由需求方拍板「不推进」**（O-9 取 (a) 维持现状，指示原文「英文界面不用推进了」；决策与量化依据见 `docs/对标差距推进计划.md` §3.8 与 §6 的 O-9 行）。⇒ 本文 §8「最佳实践」～§11「检查清单」里的"添加新语言/多语言覆盖"那些章**不作为交付目标**，保留作日后改主意时的操作参考。既有 v1 管道（语言开关／230 条译文／`.qm` 随构建部署）**原样保留不拆**；「英文模式下参数面板、运行日志、算子中文名仍是中文」从此是**明示的长期限制**，不是待补的缺口。
>
> **不受该拍板影响的一半**：`englishName`／`aliases` 参与算子搜索（SRS FR1.7／FR18.4）是**搜索能力**，中文界面下同样有用 ⇒ 仍是待办，别把它跟"英文界面"一起结案。

---

## 1. 国际化架构

### 1.1 Qt 国际化框架

#### 核心组件
- **QTranslator**: 翻译器，加载翻译文件
- **QCoreApplication::translate()**: 翻译函数
- **QObject::tr()**: 简化的翻译函数
- **Qt Linguist**: 翻译工具

#### 翻译流程
```
源代码 → 提取翻译字符串 → 翻译 → 生成翻译文件 → 加载翻译
```

### 1.2 项目结构

```
VisionFlowPlatform/
├── translations/                    # 翻译文件目录
│   ├── visionflow_zh_CN.ts         # 中文翻译源文件
│   ├── visionflow_zh_CN.qm         # 中文翻译编译文件
│   ├── visionflow_en_US.ts         # 英文翻译源文件
│   ├── visionflow_en_US.qm         # 英文翻译编译文件
│   └── ...                         # 其他语言
├── src/                            # 源代码
│   └── ...                         # 使用 tr() 包装字符串
└── CMakeLists.txt                  # 构建配置
```

> ⚠️ **就地提醒（2026-09-27）**：上面这棵树是**通用写法**，那四个文件名（`visionflow_zh_CN.ts/.qm`、`visionflow_en_US.ts/.qm`）**本仓都不存在**。`ls translations` 的实际结果是三份：`visionflow_en.ts`／`visionflow_en.qm`／`apply_en_translations.py`。详见文首「现状声明」。

---

## 2. 源代码国际化

### 2.1 使用 tr() 函数

#### 基本用法
```cpp
// 好：使用 tr() 包装字符串
QString text = tr("Hello, World!");
ui->label->setText(tr("Welcome"));

// 不好：硬编码字符串
QString text = "Hello, World!";
ui->label->setText("Welcome");
```

#### 带参数的翻译
```cpp
// 使用 arg() 替换参数
QString message = tr("Processing image %1 of %2").arg(current).arg(total);
ui->statusLabel->setText(message);

// 使用 QString::arg() 链式调用
QString message = tr("File %1 saved to %2").arg(fileName).arg(directory);
```

#### 复数形式
```cpp
// 使用 tr() 处理复数
int count = 5;
QString message = tr("%n item(s) found", "", count);
// 英文: "5 items found"
// 中文: "找到 5 个项目"
```

### 2.2 上下文

#### 使用类名作为上下文
```cpp
class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    MainWindow() {
        // 自动使用 "MainWindow" 作为上下文
        setWindowTitle(tr("VisionFlowPlatform"));
        
        // 显式指定上下文
        setWindowTitle(tr("MainWindow", "VisionFlowPlatform"));
    }
};
```

#### 使用函数名作为上下文
```cpp
void MainWindow::createMenus() {
    // 使用函数名作为上下文
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open"), this, &MainWindow::open);
    fileMenu->addAction(tr("&Save"), this, &MainWindow::save);
}
```

### 2.3 注释

#### 添加翻译注释
```cpp
// 提供翻译上下文
QString text = tr("Open", "Open a file");
QString text = tr("Close", "Close a window");

// 提供额外信息
QString text = tr("OK", "Button label for confirming an action");
```

### 2.4 特殊情况

#### 不需要翻译的字符串
```cpp
// 技术术语不需要翻译
QString className = "ThresholdNode";
QString methodName = "process";

// 日志消息可以不翻译
qDebug() << "Processing started";

// 错误消息可能需要翻译
qWarning() << tr("Processing failed");
```

#### Unicode 字符
```cpp
// 使用 Unicode 转义序列
QString text = tr("Copyright \u00A9 2026");

// 使用 HTML 实体
QString text = tr("Copyright &copy; 2026");
```

---

## 3. 翻译文件管理

### 3.1 提取翻译字符串

#### 使用 lupdate
```bash
# 从源代码提取翻译字符串
lupdate -ts translations/visionflow_zh_CN.ts

# 从多个目录提取
lupdate src/ include/ -ts translations/visionflow_zh_CN.ts
```

#### CMake 集成
```cmake
# 在 CMakeLists.txt 中添加
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)

# 设置翻译文件
set(TS_FILES
    translations/visionflow_zh_CN.ts
    translations/visionflow_en_US.ts
)

# 生成翻译文件
qt_add_translations(VisionFlowPlatform
    TS_FILES ${TS_FILES}
)
```

### 3.2 翻译文件格式

#### .ts 文件结构
```xml
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>MainWindow</name>
    <message>
        <source>VisionFlowPlatform</source>
        <translation>视觉流程平台</translation>
    </message>
    <message>
        <source>&amp;File</source>
        <translation>文件(&amp;F)</translation>
    </message>
</context>
</TS>
```

#### 翻译状态
- **unfinished**: 未翻译
- **obsolete**: 已过时
- **vanished**: 已消失
- **accepted**: 已接受
- **rejected**: 已拒绝

### 3.3 编译翻译文件

#### 使用 lrelease
```bash
# 编译翻译文件
lrelease translations/visionflow_zh_CN.ts

# 编译所有翻译文件
lrelease translations/*.ts
```

#### CMake 自动编译
```cmake
# CMake 会自动编译 .ts 文件为 .qm 文件
qt_add_translations(VisionFlowPlatform
    TS_FILES ${TS_FILES}
)
```

---

## 4. 加载翻译

### 4.1 加载翻译文件

#### 基本加载
```cpp
#include <QTranslator>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // 加载翻译文件
    QTranslator translator;
    if (translator.load("visionflow_zh_CN", "translations")) {
        app.installTranslator(&translator);
    }
    
    MainWindow window;
    window.show();
    
    return app.exec();
}
```

#### 自动检测语言
```cpp
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // 检测系统语言
    QString locale = QLocale::system().name();  // 例如 "zh_CN"
    
    // 加载对应翻译
    QTranslator translator;
    if (translator.load("visionflow_" + locale, "translations")) {
        app.installTranslator(&translator);
    }
    
    MainWindow window;
    window.show();
    
    return app.exec();
}
```

### 4.2 运行时切换语言

#### 语言切换管理器
```cpp
class LanguageManager : public QObject {
    Q_OBJECT
    
public:
    static LanguageManager &instance() {
        static LanguageManager manager;
        return manager;
    }
    
    void setLanguage(const QString &locale) {
        // 移除旧翻译
        if (m_translator) {
            qApp->removeTranslator(m_translator);
            delete m_translator;
        }
        
        // 加载新翻译
        m_translator = new QTranslator(this);
        if (m_translator->load("visionflow_" + locale, "translations")) {
            qApp->installTranslator(m_translator);
            emit languageChanged(locale);
        }
    }
    
    QStringList availableLanguages() const {
        return {"zh_CN", "en_US", "ja_JP"};
    }
    
signals:
    void languageChanged(const QString &locale);
    
private:
    QTranslator *m_translator = nullptr;
};
```

### 4.3 语言选择对话框

#### 语言选择 UI
```cpp
class LanguageDialog : public QDialog {
    Q_OBJECT
    
public:
    LanguageDialog(QWidget *parent = nullptr) : QDialog(parent) {
        QVBoxLayout *layout = new QVBoxLayout(this);
        
        // 语言选择下拉框
        m_languageCombo = new QComboBox();
        m_languageCombo->addItem("中文", "zh_CN");
        m_languageCombo->addItem("English", "en_US");
        m_languageCombo->addItem("日本語", "ja_JP");
        layout->addWidget(m_languageCombo);
        
        // 确定按钮
        QPushButton *okButton = new QPushButton(tr("OK"));
        connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
        layout->addWidget(okButton);
        
        setWindowTitle(tr("Select Language"));
    }
    
    QString selectedLanguage() const {
        return m_languageCombo->currentData().toString();
    }
    
private:
    QComboBox *m_languageCombo;
};
```

---

## 5. 特殊翻译场景

### 5.1 动态字符串

#### 运行时生成的字符串
```cpp
// 使用 QObject::tr() 的重载版本
QString errorMessage = QObject::tr("Error", "File not found");

// 使用 QCoreApplication::translate()
QString text = QCoreApplication::translate("MainWindow", "Welcome");
```

### 5.2 复数形式

#### 处理复数
```cpp
// 英文复数规则
int count = 5;
QString message = tr("%n item(s) found", "", count);
// count = 1: "1 item found"
// count = 5: "5 items found"

// 中文没有复数形式
// count = 1: "找到 1 个项目"
// count = 5: "找到 5 个项目"
```

### 5.3 日期和时间

#### 本地化日期时间
```cpp
QDateTime dateTime = QDateTime::currentDateTime();

// 使用系统格式
QString dateStr = dateTime.date().toString(Qt::DefaultLocaleShortDate);

// 使用自定义格式
QString dateStr = dateTime.toString(tr("yyyy-MM-dd hh:mm:ss"));

// 使用 QLocale
QLocale locale(QLocale::Chinese);
QString dateStr = locale.toString(dateTime, "yyyy年MM月dd日 hh:mm:ss");
```

### 5.4 数字和货币

#### 本地化数字
```cpp
double number = 1234567.89;

// 使用系统格式
QString numStr = QLocale::system().toString(number);

// 使用自定义格式
QString numStr = QLocale(QLocale::Chinese).toString(number, 'f', 2);

// 货币格式
QString currencyStr = QLocale::system().toCurrencyString(number);
```

---

## 6. 翻译工作流

### 6.1 翻译流程

```
1. 开发者编写代码，使用 tr() 包装字符串
2. 运行 lupdate 提取字符串到 .ts 文件
3. 翻译人员使用 Qt Linguist 翻译
4. 运行 lrelease 编译 .qm 文件
5. 程序加载 .qm 文件显示翻译
```

### 6.2 翻译协作

#### 版本控制
- .ts 文件纳入版本控制
- .qm 文件不纳入版本控制（自动生成）
- 翻译人员提交 .ts 文件更改

#### 翻译审核
1. 翻译人员完成翻译
2. 审核人员审核翻译
3. 开发人员测试翻译
4. 合并到主分支

### 6.3 翻译更新

#### 增量更新
```bash
# 只更新新增的字符串
lupdate -noobsolete -ts translations/visionflow_zh_CN.ts
```

#### 合并翻译
```bash
# 合并多个翻译文件
lconvert -i old.ts new.ts -o merged.ts
```

---

## 7. 测试国际化

### 7.1 测试策略

#### 功能测试
- 验证所有字符串都使用 tr()
- 验证翻译文件完整性
- 验证语言切换功能

#### UI 测试
- 验证翻译后 UI 布局
- 验证长文本显示
- 验证特殊字符显示

### 7.2 测试工具

#### 自动化测试
```cpp
void InternationalizationTest::testTranslationCompleteness() {
    // 加载翻译文件
    QTranslator translator;
    translator.load("visionflow_zh_CN", "translations");
    
    // 检查关键字符串是否翻译
    QVERIFY(!translator.translate("MainWindow", "VisionFlowPlatform").isEmpty());
    QVERIFY(!translator.translate("MainWindow", "&File").isEmpty());
}
```

#### 手动测试
```bash
# 切换语言测试
set LANG=zh_CN
./build/bin/VisionFlowPlatform

set LANG=en_US
./build/bin/VisionFlowPlatform
```

### 7.3 常见问题

#### 翻译不显示
```cpp
// 检查翻译文件是否加载
if (!translator.load("visionflow_zh_CN", "translations")) {
    qWarning() << "Failed to load translation file";
}

// 检查翻译文件路径
qDebug() << "Translation file:" << QFileInfo("translations/visionflow_zh_CN.qm").absoluteFilePath();
```

#### 字符串未翻译
```cpp
// 确保使用 tr()
// 好
QString text = tr("Hello");

// 不好
QString text = "Hello";
```

---

## 8. 最佳实践

### 8.1 字符串管理

#### 命名规范
```cpp
// 使用描述性的源字符串
QString text = tr("Save changes before closing?");

// 避免歧义
QString text = tr("Open", "Open a file");
QString text = tr("Close", "Close a window");
```

#### 上下文组织
```cpp
// 按功能模块组织上下文
// MainWindow - 主窗口
// ThresholdNode - 阈值节点
// FlowExecutor - 流程执行器
```

### 8.2 翻译质量

#### 翻译指南
- 保持一致性
- 考虑上下文
- 避免机器翻译
- 请母语人员审核

#### 术语表
```cpp
// 建立项目术语表
// Node - 节点
// Flow - 流程
// Port - 端口
// Connection - 连线
```

### 8.3 性能考虑

#### 翻译文件大小
- 只包含需要的翻译
- 使用增量更新
- 定期清理未使用的翻译

#### 加载性能
- 在启动时加载翻译
- 缓存翻译结果
- 避免频繁切换语言

---

## 9. 支持的语言

### 9.1 计划支持的语言

| 语言 | 代码 | 状态 |
|------|------|------|
| 简体中文 | zh_CN | 主要支持 |
| 英语 | en_US | 主要支持 |
| 日语 | ja_JP | 计划支持 |
| 韩语 | ko_KR | 计划支持 |
| 德语 | de_DE | 计划支持 |
| 法语 | fr_FR | 计划支持 |

> ⚠️ **就地提醒（2026-09-27）**：这张表**从未反映过仓内实况**——`src/I18n.cpp` 只认 `"zh_CN"` 与 `"en"` 两个代码，日／韩／德／法四行既没有 `.ts/.qm` 也没有菜单项（语言菜单只有「简体中文／English」两项，`src/MainWindow.cpp:1097`～`:1102`）。而且 2026-09-27 需求方已拍板**不推进英文界面交付**（O-9 取 (a)）⇒ 本表按"历史设想"留档，**不作为待办**；下面 §9.2「添加新语言」那套步骤同理。

### 9.2 添加新语言

#### 步骤 1：创建翻译文件
```bash
lupdate -ts translations/visionflow_new_lang.ts
```

#### 步骤 2：翻译字符串
使用 Qt Linguist 打开 .ts 文件进行翻译

#### 步骤 3：编译翻译文件
```bash
lrelease translations/visionflow_new_lang.ts
```

#### 步骤 4：添加语言选项
```cpp
// 在语言选择对话框中添加
m_languageCombo->addItem("新语言", "new_lang");
```

#### 步骤 5：测试翻译
切换到新语言并测试所有功能

---

## 10. 工具和资源

### 10.1 Qt Linguist

#### 功能介绍
- 可视化翻译界面
- 翻译记忆库
- 术语表管理
- 翻译状态跟踪

#### 使用方法
```bash
# 打开 Qt Linguist
linguist translations/visionflow_zh_CN.ts
```

### 10.2 命令行工具

#### lupdate
```bash
# 提取翻译字符串
lupdate -ts translations/visionflow_zh_CN.ts

# 更新现有翻译
lupdate -noobsolete -ts translations/visionflow_zh_CN.ts
```

#### lrelease
```bash
# 编译翻译文件
lrelease translations/visionflow_zh_CN.ts

# 编译所有翻译文件
lrelease translations/*.ts
```

### 10.3 在线翻译服务

#### 集成翻译 API
```cpp
// 使用 Google Translate API
QString translateText(const QString &text, const QString &targetLang) {
    // 调用翻译 API
    // ...
}
```

---

## 11. 国际化检查清单

### 11.1 代码检查
- [ ] 所有用户可见字符串都使用 tr()
- [ ] 字符串拼接使用 arg() 而不是 +
- [ ] 复数形式使用正确的参数
- [ ] 避免字符串拼接导致的翻译问题

### 11.2 UI 检查
- [ ] UI 控件有足够的空间显示翻译
- [ ] 长文本不会破坏布局
- [ ] 特殊字符正确显示
- [ ] 图标和图片支持国际化

### 11.3 功能检查
- [ ] 语言切换功能正常
- [ ] 翻译文件正确加载
- [ ] 所有语言都经过测试
- [ ] 翻译质量经过审核

### 11.4 性能检查
- [ ] 翻译文件大小合理
- [ ] 语言切换响应时间可接受
- [ ] 翻译加载不影响启动性能
- [ ] 内存使用合理

---

## 12. 常见问题解答

### Q1: 如何添加新语言支持？
A1: 
1. 使用 lupdate 创建新的 .ts 文件
2. 使用 Qt Linguist 翻译字符串
3. 使用 lrelease 编译 .qm 文件
4. 在程序中加载新的翻译文件

### Q2: 翻译字符串中如何使用变量？
A2: 使用 arg() 函数：
```cpp
QString message = tr("Processing %1 of %2").arg(current).arg(total);
```

### Q3: 如何处理复数形式？
A3: 使用 tr() 的复数形式：
```cpp
QString message = tr("%n item(s)", "", count);
```

### Q4: 如何测试翻译是否完整？
A4: 
1. 运行 lupdate 检查未翻译字符串
2. 切换语言测试所有界面
3. 检查翻译文件的完成度

### Q5: 如何优化翻译文件大小？
A5: 
1. 移除未使用的翻译
2. 使用增量更新
3. 压缩翻译文件
4. 只包含需要的语言

---

## 13. 总结

通过本指南，您可以：

1. **理解国际化架构**: Qt 国际化框架、翻译流程
2. **实现源代码国际化**: 使用 tr()、参数替换、复数形式
3. **管理翻译文件**: 提取、翻译、编译、加载
4. **测试国际化功能**: 功能测试、UI 测试、性能测试
5. **遵循最佳实践**: 字符串管理、翻译质量、性能优化

国际化支持将帮助 VisionFlowPlatform 服务全球用户，提升用户体验和市场竞争力。
