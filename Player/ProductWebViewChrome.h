#pragma once

#include <QtGui/qwindowdefs.h>
#include <memory>

class QByteArray;
class QString;
class QWidget;
struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2CompositionController;
struct IUnknown;

namespace DesktopSceneEngine::Player
{
// A single opaque WebView surface. The Main Session attaches to its existing
// Model C tree instead, which also owns the Engine presentation plane.
class OpaqueWebViewVisual final
{
public:
    explicit OpaqueWebViewVisual(WId window);
    ~OpaqueWebViewVisual();
    [[nodiscard]] IUnknown* visual() const noexcept;
    [[nodiscard]] long commit() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void closeProductCompositionController(ICoreWebView2CompositionController* value) noexcept;

// These messages use the enclosing Host's existing exact source and page
// admission. This binding owns no window, controller, or product command.
class ProductWebViewChrome final
{
public:
    ProductWebViewChrome();
    ~ProductWebViewChrome();
    void reset();
    [[nodiscard]] bool isModalSuspended(const QString& generation) const;
    void refresh(QWidget* input, ICoreWebView2Controller* controller, ICoreWebView2* web,
        const QString& generation, bool productReady);
    bool receive(const QByteArray& packet, const QString& generation, QWidget* input,
        ICoreWebView2Controller* controller, ICoreWebView2* web, bool productReady);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
