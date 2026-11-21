#pragma once

#include <QJsonObject>
#include <functional>
#include <memory>

class QWidget;
struct ICoreWebView2CompositionController;
struct ICoreWebView2Controller;

namespace DesktopSceneEngine::Player
{
// Client input and accessibility for a composed WebView. ProductWindowHost
// owns non-client input; the WebView Host owns the controller and page.
class SessionCompositionInput final
{
public:
    SessionCompositionInput(QWidget& host, ICoreWebView2CompositionController* composition,
        ICoreWebView2Controller* controller, std::function<bool()> acceptsInput);
    ~SessionCompositionInput();
    SessionCompositionInput(const SessionCompositionInput&) = delete;
    SessionCompositionInput& operator=(const SessionCompositionInput&) = delete;
    // Retire queued physical-routing work across a page generation, retaining
    // the one controller/filter. No page/domain identity is owned here.
    void invalidate() noexcept;
    void retire() noexcept;
    [[nodiscard]] QJsonObject diagnosticFacts() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
