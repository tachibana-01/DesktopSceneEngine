#include "BgmPeerWindow.h"
#include <QCloseEvent>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>
#include <QtCore/qt_windows.h>
#include <dwmapi.h>
#include <stdexcept>
#include <utility>

namespace DesktopSceneEngine::Player
{
BgmPeerWindow::BgmPeerWindow(QUuid session, WebViewSessionHost* environmentOwner,
    WebViewSessionHost::BgmPresentation presentation, WebViewSessionHost::RuntimePresentation runtime,
    std::function<void()> retired, WebViewSessionHost::ControlSubmit control,
    std::function<bool()> closeBlocked)
    : ProductWindowHost(ProductWindowRole::BackgroundMusic),
      session_(std::move(session)), presentation_(std::move(presentation)),
      runtime_(std::move(runtime)), control_(std::move(control)), retired_(std::move(retired)),
      closeBlocked_(std::move(closeBlocked))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::NonModal);
    resize(1060, 760);
    setMinimumSize(720, 500);
    presentation_.close = [this] { QTimer::singleShot(0, this, [this] { close(); }); };
    createHost(environmentOwner);
}
BgmPeerWindow::~BgmPeerWindow() { retire(); }

bool BgmPeerWindow::showForFirstPresentation()
{
    const QPointer<BgmPeerWindow> current{this};
    const auto withdraw = [&] {
        // Drop the permit and Host even if native window creation failed.
        // Later WebView failures still use the visible product surface.
        if (current)
        {
            qWarning("[BgmPeer] initial native presentation could not be revealed");
            current->hide();
            if (current) current->retire();
            if (current) current->deleteLater();
        }
        return false;
    };
    const auto initial = winId();
    if (!current || !initial) return withdraw();
    const auto hwnd = reinterpret_cast<HWND>(initial);
    const BOOL cloak = TRUE;
    const bool staged = !isVisible() && SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak)));
    const auto sameWindow = [&] { return current && current->internalWinId() == initial && IsWindow(hwnd); };
    const auto reveal = [&] {
        if (!sameWindow()) return false;
        const BOOL uncloak = FALSE;
        const auto status = staged ? DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &uncloak, sizeof(uncloak)) : S_OK;
        DWORD state{};
        if (!sameWindow()) return false;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &state, sizeof(state)))) return state == 0;
        return SUCCEEDED(status);
    };
    auto restoreCloak = qScopeGuard([&] { (void)reveal(); });
    if (sameWindow() && current->retired_) current->show();
    if (staged && sameWindow())
    {
        // Populate the compositor's initial backing while the HWND is still
        // cloaked. Restore it before the caller raises or activates the peer.
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        DwmFlush();
    }
    const bool visible = reveal() && sameWindow() && current->retired_ && current->isVisible();
    restoreCloak.dismiss();
    if (!visible) return withdraw();
    return true;
}

void BgmPeerWindow::createHost(WebViewSessionHost* environmentOwner)
{
    // Native creation delivers WinIdChange before WebView startup. A failed
    // frame retains the visible product failure and ordinary X.
    (void)winId();
    if (productFrameFailed())
    {
        qWarning("[BgmPeer] native product frame unavailable");
        return;
    }
    host_ = std::make_unique<WebViewSessionHost>(session_, productViewport(), presentation_, runtime_, control_, environmentOwner);
    host_->setProductPresentationObserver(
        [guard = QPointer<BgmPeerWindow>{this}, host = QPointer<WebViewSessionHost>{host_.get()}, firstReady = true]
        (ProductPresentationStage stage, const QString& detail) mutable {
            if (!guard || !host || guard->host_.get() != host.data()
                || (guard->productFrameFailed() && stage != ProductPresentationStage::Retired))
                return;
            guard->setProductPresentationStage(stage, detail);
            if (stage != ProductPresentationStage::Ready || !guard->isProductPresentationAvailable()
                || !std::exchange(firstReady, false))
                return;
            // Keep the native loading surface while the first controller and
            // trusted document start. Once attached, the viewport stays across reload.
            guard->setProductContent(host.data());
            guard->setFocusProxy(host.data());
            // Bootstrap can finish after the native peer has received focus.
            QTimer::singleShot(0, host.data(), [guard, host] {
                if (guard && host && guard->host_.get() == host.data() && guard->isProductPresentationAvailable())
                    host->reconcileForegroundKeyboardFocus(guard->effectiveWinId());
            });
        });
}
void BgmPeerWindow::retire()
{
    if (host_) host_->closeHost();
    if (auto callback = std::move(retired_)) callback();
}
void BgmPeerWindow::closeEvent(QCloseEvent* event)
{
    if (closeBlocked_ && closeBlocked_())
    {
        closeAfterModal_ = true;
        event->ignore();
        return;
    }
    retire();
    ProductWindowHost::closeEvent(event);
}
void BgmPeerWindow::resumeModalClose()
{
    if (std::exchange(closeAfterModal_, false))
        QTimer::singleShot(0, this, [this] { close(); });
}
void BgmPeerWindow::publish()
{
    if (!retired_) return;
    // The Host owns truthful unavailable presentation on provider failure.
    if (host_)
    {
        host_->publishAudioEffects();
        host_->publishBgmEditorState();
        host_->publishBgmRuntimeState();
    }
    try
    {
        const auto header = presentation_.header();
        const auto prefix = header.value("language") == QJsonValue{"zh_CN"} ? QStringLiteral("背景音乐") : QStringLiteral("Background Music");
        setWindowTitle(prefix + QStringLiteral(" — ") + header.value("playlistAlias").toString());
    }
    catch (const std::exception&) { setWindowTitle(QStringLiteral("Background Music — unavailable")); }
}
}
