// 国际化辅助（G-P1-5）：语言持久化 + 翻译文件加载。
// 设计原则：源文即简体中文；仅当需要英文时才加载 visionflow_en.qm。
#pragma once

#include <QString>

class QApplication;

namespace I18n {

enum class Language {
    Chinese, // 源文语言（无需翻译文件）
    English
};

// 读取持久化语言，默认 Chinese。
Language currentLanguage();

// 写回持久化语言（立即 sync）。
void setLanguage(Language lang);

// 加载当前语言对应的翻译文件。必须在 QApplication 构造后、MainWindow 构造前调用。
// 返回是否成功（Chinese 视为成功，English 在 .qm 缺失时也返回 true 但不翻译）。
bool installTranslators(QApplication &app);

// 语言显示名（用于菜单）。
QString displayName(Language lang);

// 语言对应的 .qm 基名（如 "visionflow_en"）。
QString qmBaseName(Language lang);

} // namespace I18n
