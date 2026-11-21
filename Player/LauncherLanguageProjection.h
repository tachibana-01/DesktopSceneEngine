#pragma once

#include "ApplicationPreferences.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace DesktopSceneEngine::Player
{
struct LauncherLanguageProjection
{
    std::optional<UiLanguage> committed;
    UiLanguage effective = UiLanguage::English;
    quint64 revision = 0;
};

inline QString launcherLanguageCode(UiLanguage language)
{
    return language == UiLanguage::SimplifiedChinese
        ? QStringLiteral("zh_CN") : QStringLiteral("en");
}

inline std::optional<UiLanguage> parseLauncherLanguage(const QString& code)
{
    if (code == QStringLiteral("en")) return UiLanguage::English;
    if (code == QStringLiteral("zh_CN")) return UiLanguage::SimplifiedChinese;
    return std::nullopt;
}

inline QJsonObject serializeLauncherLanguage(const LauncherLanguageProjection& language)
{
    QJsonObject result{{QStringLiteral("readStatus"), language.committed
            ? QStringLiteral("Success") : QStringLiteral("Failure")},
        {QStringLiteral("effective"), launcherLanguageCode(language.effective)},
        {QStringLiteral("revision"), static_cast<qint64>(language.revision)}};
    if (language.committed)
        result.insert(QStringLiteral("committed"), launcherLanguageCode(*language.committed));
    return result;
}
}
