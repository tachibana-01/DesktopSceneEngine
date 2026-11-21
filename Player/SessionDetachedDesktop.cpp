#include "SessionApplication.h"

#include <QPointer>
#include <QTimer>
#include <utility>

namespace DesktopSceneEngine::Player
{
void SessionApplication::requestDetachedDesktopDeployment(DesktopDeploymentIntent intent,
    DesktopDeploymentCompletion complete)
{
    requireExecutor();
    if (!complete) return;
    if (!autonomousRuntime_ || !engineInitialized_ || !platform_ || desktopReplacementInProgress_
        || closePending() || closing())
    {
        complete(DesktopDeploymentResult::Rejected);
        return;
    }
    const bool deploy = intent == DesktopDeploymentIntent::Deploy;
    if (deploy == desktopDeploymentProjection().retained)
    {
        const auto facts = platformFacts();
        const bool satisfied = !deploy || (desktopDeploymentDesired_ && facts.desktopTargetRegistered
            && facts.desktopHostOwned && facts.desktopHealthy);
        // A retained failed or suspended endpoint still owns cleanup. It must
        // neither be silently replaced nor acknowledged as a healthy Deploy.
        complete(satisfied ? DesktopDeploymentResult::AlreadySatisfied : DesktopDeploymentResult::Rejected);
        return;
    }
    if (deploy)
    {
        // The initial desktop frame must use the same resolved mode as ordinary frames.
        if (!initialCanvasOutsideModeReady())
        {
            complete(DesktopDeploymentResult::Rejected);
            return;
        }
        refreshDisplaySelection();
        if (!displaySelection_.boundDisplay())
        {
            complete(DesktopDeploymentResult::Rejected);
            return;
        }
    }
    desktopDeploymentDesired_ = deploy;
    dispatchDetachedDesktop(false, std::move(complete));
}

void SessionApplication::reconcileDetachedDesktopTopology(bool allowContinuation)
{
    requireExecutor();
    if (!autonomousRuntime_ || !engineInitialized_ || !platform_ || closing()) return;
    if (desktopReplacementInProgress_)
    {
        detachedDesktopTopologyDirty_ = true;
        return;
    }
    refreshDisplaySelection();
    if (desktopDeploymentDesired_ || platformFacts().desktopRetained)
        dispatchDetachedDesktop(allowContinuation);
}

void SessionApplication::dispatchDetachedDesktop(bool allowContinuation, DesktopDeploymentCompletion complete)
{
    desktopReplacementInProgress_ = true;
    const bool commanded = bool(complete);
    detachedDesktopCommandPending_ = commanded;
    detachedDesktopDispatch_ = QUuid::createUuid();
    updateDesktopDeploymentButton();
    publishReadOnlyInvalidation();
    auto* const source = platform_;
    const auto settled = [owner = QPointer<SessionApplication>{this}, token = detachedDesktopDispatch_,
        source, allowContinuation, commanded, complete = std::move(complete)](SessionDesktopResult result) {
        if (!owner) return;
        owner->requireExecutor();
        if (!owner->desktopReplacementInProgress_ || owner->detachedDesktopDispatch_ != token) return;
        owner->desktopReplacementInProgress_ = false;
        owner->detachedDesktopCommandPending_ = false;
        const bool active = owner->engineInitialized_ && !owner->closing() && owner->platform_ == source;
        if (active && result.facts) owner->observePresentationFacts(*result.facts);
        const bool success = active && result.attempt == SessionDesktopAttempt::Ready;
        if (commanded && !success) owner->desktopDeploymentDesired_ = false;
        owner->updateDesktopDeploymentButton();
        owner->publishReadOnlyInvalidation();
        if (complete)
            complete(!active ? DesktopDeploymentResult::Rejected
                : success ? DesktopDeploymentResult::Completed : DesktopDeploymentResult::Failure);
        const bool topologyDirty = std::exchange(owner->detachedDesktopTopologyDirty_, false);
        if (!active) return;
        if (topologyDirty)
            QTimer::singleShot(0, owner.data(), [owner] { if (owner) owner->reconcileDetachedDesktopTopology(); });
        else if (allowContinuation && owner->desktopDeploymentDesired_
            && result.attempt == SessionDesktopAttempt::TransientlyUnavailable)
            QTimer::singleShot(250, owner.data(), [owner, token] {
                if (owner && owner->detachedDesktopDispatch_ == token && owner->desktopDeploymentDesired_)
                    owner->reconcileDetachedDesktopTopology(false);
            });
    };
    std::optional<SessionDesktopFrame> frame;
    if (desktopDeploymentDesired_)
        frame = SessionDesktopFrame{engine_.presentationInput(workspaceModel_.desktopCanvas().aspectRatio),
            currentCanvasOutsideMode_};
    if (!source->requestDetachedDesktop(displaySelection_.boundDisplayId(), std::move(frame), settled))
        settled({});
}
}
