#pragma once

#include <DesktopSceneEngine/Platform/Display.h>
#include "ApplicationPreferences.h"
#include "GlobalSettingsNotification.h"
#include "SessionPresentation.h"

#include <QMainWindow>

#include <optional>
#include <functional>
#include <vector>

class QLabel;
class QListWidget;
class QPushButton;
class QWidget;
class QToolButton;

class LauncherWindowTestAccess;

namespace DesktopSceneEngine::Player
{

class LauncherWindow final : public QMainWindow
{
public:

    LauncherWindow(ApplicationTranslationState& translationState,
        ApplicationPreferencesStore& preferencesStore, QWidget* parent = nullptr);

    // Called only on accepted close, before role teardown. Bootstrap owns the resource it stops.
    void setAcceptedCloseCallback(std::function<void()> callback) { acceptedCloseCallback_ = std::move(callback); }
    SessionPresentation& trayPresentation() { return trayPresentation_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:

    friend class ::LauncherWindowTestAccess;

    void openSettings();
    void commitLanguage(UiLanguage language, bool (&publish)() = broadcastGlobalSettingsChanged);
    void retranslateUi();
    void updateStatus();

    void refreshDisplays();

    void applyDisplayEnumeration(const Platform::DisplayEnumerationResult& result);

    void identifySelectedDisplay();

    void startSelectedDisplaySession();

    void updateActions();

    [[nodiscard]] std::optional<DisplayId> selectedDisplayId() const;

    QListWidget* displayList_ = nullptr;
    QLabel* instructionLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* identifyButton_ = nullptr;
    QPushButton* startSessionButton_ = nullptr;
    QPushButton* settingsButton_ = nullptr;
    QToolButton* hideToTrayButton_ = nullptr;
    SessionPresentation trayPresentation_{*this};
    ApplicationTranslationState& translationState_;
    ApplicationPreferencesStore& preferencesStore_;
    // Literal source text for the last action result; translated text is derived.
    const char* statusSourceText_ = nullptr;
    Platform::DisplayEnumerationResult displayEnumeration_{};
    std::vector<DisplayId> rowDisplayIds_{};
    std::function<void()> acceptedCloseCallback_;
};

}
