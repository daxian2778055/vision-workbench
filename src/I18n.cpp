#include "I18n.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QTranslator>

namespace I18n {

Language currentLanguage()
{
    QSettings s(QStringLiteral("VisionFlow"), QStringLiteral("VisionFlowPlatform"));
    return s.value(QStringLiteral("Language"), QStringLiteral("zh_CN")).toString() ==
                   QStringLiteral("en")
               ? Language::English
               : Language::Chinese;
}

void setLanguage(Language lang)
{
    QSettings s(QStringLiteral("VisionFlow"), QStringLiteral("VisionFlowPlatform"));
    s.setValue(QStringLiteral("Language"),
               lang == Language::English ? QStringLiteral("en") : QStringLiteral("zh_CN"));
    s.sync();
}

QString qmBaseName(Language lang)
{
    return lang == Language::English ? QStringLiteral("visionflow_en")
                                     : QStringLiteral("visionflow_zh_CN");
}

QString displayName(Language lang)
{
    return lang == Language::English ? QStringLiteral("English")
                                     : QStringLiteral("简体中文");
}

bool installTranslators(QApplication &app)
{
    const Language lang = currentLanguage();
    // 源文即中文，无需加载任何翻译文件
    if (lang == Language::Chinese)
        return true;

    const QString qm = QCoreApplication::applicationDirPath() +
                      QStringLiteral("/translations/") + qmBaseName(lang) +
                      QStringLiteral(".qm");

    // 静态生命周期：须覆盖整个 app 运行期
    static QTranslator translator;
    if (translator.load(qm)) {
        app.installTranslator(&translator);
        return true;
    }
    // AB2：仍降级为源文（中文）不阻断启动，但必须留痕——英文现场此前是静默中文，
    // 构建/部署/发布三道闸各自又都放行，现场和研发都看不出问题。
    qWarning().noquote() << QStringLiteral("英文翻译加载失败：%1（界面将退回中文源文）").arg(qm);
    return false;
}

} // namespace I18n
