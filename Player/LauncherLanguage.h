#pragma once

#include "GlobalSettingsNotification.h"
#include "LauncherLanguageProjection.h"

#include <QObject>

#include <functional>
#include <memory>

namespace DesktopSceneEngine::Player
{
// Coordinator-owned preference authority; its native invalidation endpoint lives
// independently of any particular WebView Launcher presentation.
class LauncherLanguage final : public QObject
{
public:
    explicit LauncherLanguage(ApplicationPreferencesStore& store,
        ApplicationTranslationState& translation,
        std::function<bool()> publish = broadcastGlobalSettingsChanged);
    ~LauncherLanguage() override;

    [[nodiscard]] const LauncherLanguageProjection& current() const noexcept;
    [[nodiscard]] LauncherLanguageProjection reload();
    void consumeInvalidation();
    [[nodiscard]] QJsonObject commit(UiLanguage requested);
    void setOnChanged(std::function<void(const LauncherLanguageProjection&)> callback);

private:
    class Receiver;
    LauncherLanguageProjection update(std::optional<UiLanguage> committed, UiLanguage effective);

    ApplicationPreferencesStore& store_;
    ApplicationTranslationState& translation_;
    std::function<bool()> publish_;
    LauncherLanguageProjection projection_;
    std::function<void(const LauncherLanguageProjection&)> onChanged_;
    std::unique_ptr<Receiver> receiver_;
};
}
