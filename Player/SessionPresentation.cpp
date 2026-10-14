#include "SessionPresentation.h"
#include "InstanceActivation.h"

#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QRawFont>
#include <QToolButton>
#include <QWindow>
#include <windows.h>
#include <iostream>

namespace DesktopSceneEngine::Player
{
SessionPresentation::SessionPresentation(QWidget& window) : window_(&window)
{
    window.installEventFilter(this);
}

void SessionPresentation::bindButton(QToolButton& button)
{
    // Keep the client action's chevron independent of the localized text font.
    // These are existing Windows fonts, not shipped assets or native caption UI.
    auto glyphFont = button.font();
    glyphFont.setPixelSize(18); // Legible Unicode fallback if neither icon font exists.
    button.setFont(glyphFont);
    const auto families = QFontDatabase::families();
    for (const auto* family : {"Segoe Fluent Icons", "Segoe MDL2 Assets"})
    {
        if (!families.contains(QString::fromLatin1(family))) continue;
        QFont iconFont(QString::fromLatin1(family));
        iconFont.setPixelSize(16);
        if (!QRawFont::fromFont(iconFont).supportsCharacter(0xE70D)) continue;
        button.setText(QString(QChar(0xE70D))); // Windows ChevronDown; accessible name stays semantic.
        button.setFont(iconFont);
        break;
    }
    button_ = &button;
    connect(&button, &QToolButton::clicked, this, [this] { requestHide(); });
    refreshEligibility();
}

void SessionPresentation::bindRecovery(std::function<bool()> qualified, std::function<void()> requestHide)
{
    qualified_ = std::move(qualified);
    requestHide_ = std::move(requestHide);
    refreshEligibility();
}

bool SessionPresentation::eligible() const
{
    return !ending_ && window_ && !closePending() && qualified_ && qualified_() && !modalBlocked();
}

bool SessionPresentation::modalBlocked() const
{
    // Application-modal BGM dialogs need not descend from MainWindow.
    if (const auto* modal = QApplication::activeModalWidget(); modal
        && (modal->windowModality() == Qt::ApplicationModal || modal == window_
            || (window_ && window_->isAncestorOf(modal)))) return true;
    // Includes native dialogs with a hidden Qt wrapper / disabled native owner.
    for (const auto* top : QApplication::topLevelWidgets())
        if (top->isVisible() && top->testAttribute(Qt::WA_WState_Created)
            && !IsWindowEnabled(reinterpret_cast<HWND>(top->effectiveWinId()))) return true;
    return false;
}

bool SessionPresentation::closePending() const
{
    return closeOperation_ && closeOperation_->result == RoleCloseResult::Pending;
}

bool SessionPresentation::canClose() const
{
    return !ending_ && window_ && !closePending() && !modalBlocked();
}

RoleCloseResult SessionPresentation::queryClose(const RoleCloseKey& key) const
{
    if (closeOperation_ && closeOperation_->key == key) return closeOperation_->result;
    if (lastRejectedClose_ && *lastRejectedClose_ == key) return RoleCloseResult::Rejected;
    return RoleCloseResult::Uncertain; // Never reconstruct a decision or execute Close from a query.
}

RoleCloseResult SessionPresentation::requestClose(const RoleCloseKey& key)
{
    if (key.generation.isNull() || !key.request) return RoleCloseResult::Rejected;
    if (closeOperation_ && closeOperation_->key == key) return closeOperation_->result;
    if (lastRejectedClose_ && *lastRejectedClose_ == key) return RoleCloseResult::Rejected;
    if (!canClose())
    {
        lastRejectedClose_ = key; // Bounded denial record, without replacing an outstanding interaction.
        return RoleCloseResult::Rejected;
    }
    closeWasHidden_ = hidden_;
    closeRecoveryLost_ = false;
    closeOperation_ = RoleCloseOperation{key, RoleCloseResult::Pending};
    refreshEligibility();
    if (closeReport_) closeReport_(); // Pending before entering a nested document/dialog event loop.
    window_->close(); // Sole semantic path: actual QWidget close -> existing role closeEvent.
    closeOperation_->result = ending_ ? RoleCloseResult::CloseAccepted : RoleCloseResult::CloseAborted;
    if (!ending_ && closeWasHidden_)
    {
        if (!closeRecoveryLost_ && qualified_ && qualified_() && !modalBlocked())
        {
            hidden_ = true;
            window_->hide(); // Preserve original saved geometry/state, even after Show during Pending.
        }
        else (void)restore();
    }
    refreshEligibility();
    if (closeReport_) closeReport_(); // Accepted only AFTER the existing ordinary teardown returned.
    return closeOperation_->result;
}

void SessionPresentation::surfaceCloseInteraction()
{
    if (closePending() && closeWasHidden_ && !ending_ && window_)
    {
        (void)restore();
        surfaceAdmittedRole(*window_);
    }
}

void SessionPresentation::refreshEligibility()
{
    if (closePending() && (!qualified_ || !qualified_())) closeRecoveryLost_ = true;
    // Loss of the recovery path is different from a temporary modal Hide blocker.
    // A hidden primary must fail visible immediately on known qualification loss.
    if (hidden_ && !ending_ && (!qualified_ || !qualified_())) (void)restore();
    if (button_) button_->setEnabled(eligible());
}

void SessionPresentation::requestHide()
{
    if (eligible() && requestHide_) requestHide_();
}

bool SessionPresentation::applyHidePermit()
{
    if (!eligible()) return false; // Fresh local qualification/modal check, not cached button state.
    if (hidden_) return true;
    restoreState_ = window_->windowState() & ~Qt::WindowMinimized & ~Qt::WindowActive;
    restoreGeometry_ = window_->isMaximized() ? window_->normalGeometry() : window_->geometry();
    hidden_ = true;
    window_->hide();
    return !window_->isVisible();
}

bool SessionPresentation::restore()
{
    if (ending_ || !window_) return false;
    ++showGeneration_; // A Show/ACTIVATE supersedes any in-flight Hide permit.
    // Restoration is separate from the explicit terminal activation intent.
    const bool priorShowWithoutActivating = window_->testAttribute(Qt::WA_ShowWithoutActivating);
    window_->setAttribute(Qt::WA_ShowWithoutActivating);
    // Qt 6.10 copies WA_ShowWithoutActivating only at HWND creation. Its backend
    // property also turns maximized Show into SW_SHOWNOACTIVATE (normal restore).
    // Guard only this synchronous native Show, preserving Qt's normal/maximized
    // path and restoring the exact extended style before any explicit ACTIVATE.
    const auto hwnd = reinterpret_cast<HWND>(window_->effectiveWinId());
    const auto priorStyle = hwnd ? GetWindowLongPtrW(hwnd, GWL_EXSTYLE) : 0;
    SetLastError(ERROR_SUCCESS);
    const bool guarded = hwnd && (SetWindowLongPtrW(hwnd, GWL_EXSTYLE, priorStyle | WS_EX_NOACTIVATE) != 0
        || GetLastError() == ERROR_SUCCESS);
    const QPointer<QWindow> nativeWindow = window_->windowHandle();
    const auto priorNativeShow = nativeWindow ? nativeWindow->property("_q_showWithoutActivating") : QVariant{};
    if (!guarded && nativeWindow)
    {
        std::cerr << "[Tray] Native non-activating restore guard unavailable; using visible Qt fallback.\n";
        nativeWindow->setProperty("_q_showWithoutActivating", true);
    }
    // Qt Windows' visible minimized-state transition activates internally.
    // Hide transiently before changing that state, then use Qt's non-activating show.
    const bool restoringHidden = hidden_;
    if (!hidden_ && window_->isMinimized()) window_->hide();
    // SW_SHOWMAXIMIZED restoring a native iconic window can activate even with
    // WS_EX_NOACTIVATE. Clear that native iconic state with the documented
    // non-activating restore, hide again, then let Qt show the saved maximized state.
    if (guarded && IsIconic(hwnd))
    { ShowWindow(hwnd, SW_SHOWNOACTIVATE); ShowWindow(hwnd, SW_HIDE); }
    if (restoringHidden)
    {
        hidden_ = false;
        window_->setWindowState(Qt::WindowNoState);
        window_->setGeometry(restoreGeometry_);
        window_->setWindowState(restoreState_);
    }
    if (window_->isMinimized()) window_->setWindowState(window_->windowState() & ~Qt::WindowMinimized);
    window_->show();
    if (guarded)
    {
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(hwnd, GWL_EXSTYLE, priorStyle) && GetLastError() != ERROR_SUCCESS)
            std::cerr << "[Tray] Native restore guard cleanup failed.\n";
    }
    else if (nativeWindow) nativeWindow->setProperty("_q_showWithoutActivating", priorNativeShow);
    window_->setAttribute(Qt::WA_ShowWithoutActivating, priorShowWithoutActivating);
    return true;
}

void SessionPresentation::beginTeardown()
{
    ending_ = true;
    ++showGeneration_;
    qualified_ = {};
    requestHide_ = {};
    if (button_) button_->setEnabled(false);
}

bool SessionPresentation::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_ && event->type() == QEvent::Show) hidden_ = false;
    return QObject::eventFilter(watched, event);
}
}
