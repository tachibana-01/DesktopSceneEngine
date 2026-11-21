#pragma once

#include "RoleClose.h"

#include <QPointer>
#include <QRect>
#include <QWidget>
#include <functional>
#include <optional>

namespace DesktopSceneEngine::Player
{
// Local primary-window presentation and ordinary-Close wrapper; never owns document/runtime state.
class SessionPresentation final : public QObject
{
public:
    using HideValidity = std::function<bool()>;

    explicit SessionPresentation(QWidget& window);
    void bindRecovery(std::function<bool()> qualified, std::function<void()> requestHide);
    void bindPermitRecovery(std::function<bool()> qualified, std::function<void(HideValidity)> requestHide);
    void bindEligibilityChanged(std::function<void(bool)> callback);
    void refreshEligibility();
    void requestHide(HideValidity stillAllowed = {});
    [[nodiscard]] bool applyHidePermit();
    [[nodiscard]] bool restore();
    void beginTeardown();
    [[nodiscard]] bool hiddenToTray() const { return hidden_; }
    [[nodiscard]] bool eligible() const;
    [[nodiscard]] quint64 showGeneration() const { return showGeneration_; }
    [[nodiscard]] bool canClose() const;
    [[nodiscard]] bool closePending() const;
    [[nodiscard]] bool ending() const { return ending_; }
    [[nodiscard]] RoleCloseResult requestClose(const RoleCloseKey& key);
    // Completion is explicit: QWidget::close() may only begin asynchronous
    // document/audio retirement and cannot determine the terminal result.
    void completeClose(bool accepted);
    [[nodiscard]] RoleCloseResult queryClose(const RoleCloseKey& key) const;
    [[nodiscard]] const std::optional<RoleCloseOperation>& closeOperation() const { return closeOperation_; }
    void bindCloseReport(std::function<void()> report) { closeReport_ = std::move(report); }
    void surfaceCloseInteraction();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QWidget> window_;
    std::function<bool()> qualified_;
    std::function<void(HideValidity)> requestHide_;
    std::function<void(bool)> eligibilityChanged_;
    std::optional<bool> lastEligibility_;
    QRect restoreGeometry_;
    Qt::WindowStates restoreState_{};
    bool hidden_ = false;
    bool ending_ = false;
    quint64 showGeneration_ = 0;
    [[nodiscard]] bool modalBlocked() const;
    std::optional<RoleCloseOperation> closeOperation_;
    std::optional<RoleCloseKey> lastRejectedClose_;
    std::function<void()> closeReport_;
    bool closeWasHidden_ = false;
    bool closeRecoveryLost_ = false;
};
}
