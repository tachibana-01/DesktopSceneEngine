#pragma once

#include <DesktopSceneEngine/DisplayIdentity.h>
#include <DesktopSceneEngine/PresentationTarget.h>

#include <QTranslator>
#include <QSettings>

#include <optional>

class QSettings;

namespace DesktopSceneEngine::Player
{

enum class UiLanguage
{
    English,
    SimplifiedChinese
};

struct ApplicationPreferences
{
    UiLanguage language = UiLanguage::English;
    bool highlightCanvasBoundary = false;
};

struct CanvasOutsidePreferenceRead
{
    std::optional<CanvasOutsideMode> value{};
    QSettings::Status status = QSettings::NoError;
};

class ApplicationTranslationState final
{
public:

    ApplicationTranslationState() = default;
    explicit ApplicationTranslationState(QString applicationTranslationPath);
    ~ApplicationTranslationState();

    ApplicationTranslationState(const ApplicationTranslationState&) = delete;
    ApplicationTranslationState& operator=(const ApplicationTranslationState&) = delete;

    [[nodiscard]] UiLanguage setLanguage(UiLanguage language);
    [[nodiscard]] UiLanguage activeLanguage() const noexcept;

private:

    QTranslator applicationTranslator_{};
    QTranslator qtBaseTranslator_{};
    UiLanguage activeLanguage_ = UiLanguage::English;
    const QString applicationTranslationPath_{};
};

class ApplicationPreferencesStore final
{
public:

    explicit ApplicationPreferencesStore(QSettings& settings) noexcept;

    [[nodiscard]] ApplicationPreferences load() const;

    QSettings::Status saveLanguage(UiLanguage language) const;

    [[nodiscard]] std::optional<UiLanguage> reloadLanguage() const;

    void saveHighlightCanvasBoundary(bool highlighted) const;

    [[nodiscard]] CanvasOutsidePreferenceRead loadCanvasOutsideMode(
        const DisplayId& displayId) const;

    QSettings::Status saveCanvasOutsideMode(
        const DisplayId& displayId,
        CanvasOutsideMode mode) const;

private:

    QSettings& settings_;
};

}
