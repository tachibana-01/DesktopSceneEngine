#pragma once

#include "LauncherRolePresentation.h"
#include "ProductWindowHost.h"
#include "SessionPresentation.h"
#include <DesktopSceneEngine/DisplayIdentity.h>

#include <QJsonObject>
#include <QWidget>
#include <QUuid>
#include <functional>
#include <memory>

namespace DesktopSceneEngine::Player
{
class LauncherLanguage;
struct LauncherLanguageProjection;
struct LauncherSessionsSnapshot;
// Coordinator-owned presentation only. Session admission, registry and process ownership stay native.
class WebViewLauncherHost final : public ProductWindowHost, public LauncherRolePresentation
{
public:
    WebViewLauncherHost(std::function<QJsonObject()> snapshotProvider,
        std::function<QJsonObject()> refreshProvider,
        std::function<QJsonObject(const DisplayId&)> identifyProvider,
        LauncherLanguage& language, QWidget* parent = nullptr);
    ~WebViewLauncherHost() override;
    void setAcceptedCloseCallback(std::function<void()> callback) { acceptedCloseCallback_ = std::move(callback); }
    SessionPresentation& trayPresentation() override { return trayPresentation_; }
    [[nodiscard]] bool isWebViewReady() const;
    [[nodiscard]] QString pageGeneration() const;
    void languageChanged(const LauncherLanguageProjection& language);
    void sessionsChanged(const LauncherSessionsSnapshot& sessions);
    void setSessionCommands(std::function<QJsonObject(const QUuid&, const DisplayId&)> create,
        std::function<QJsonObject(const QUuid&, const QUuid&)> open);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend struct LauncherHostLifetimeTestAccess;
    struct State;
    std::shared_ptr<State> state_;
    SessionPresentation trayPresentation_{*this};
    std::function<void()> acceptedCloseCallback_;
};
}
