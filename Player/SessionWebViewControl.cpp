#include "SessionWebViewControl.h"
#include "SessionApplication.h"
#include "SessionBridgeContract.h"
#include "SessionReadModel.h"

#include <QThread>
#include <utility>

namespace DesktopSceneEngine::Player
{
namespace
{
using Host = WebViewSessionHost;

bool requiresAudioDocument(const SessionBridge::Request& request)
{
    return request.documentAction || request.documentChoice || request.authored || request.runtime
        || request.bgmAuthored || (request.bgmRuntime && request.bgmRuntime->trackId);
}

bool matchesPeer(const BgmPeerIdentity& peer, const QUuid& permit, const BgmTarget& target)
{
    return permit == peer.permit && bgmPlaylistId(target) == peer.playlist;
}
}

std::optional<Host::ControlError> validateSessionWebViewCommand(const SessionApplication& application,
    const SessionReadModel& reader, const SessionBridge::Request& request, const QUuid& admittedDocument,
    const std::optional<BgmPeerIdentity>& peer)
{
    if (QThread::currentThread() != application.thread())
        return Host::ControlError{QStringLiteral("control-command-unavailable")};
    if (request.sessionIncarnation != reader.sessionIncarnation().toString(QUuid::WithoutBraces))
        return Host::ControlError{QStringLiteral("wrong-session-context")};
    if (request.pageAdmission && !request.pageAdmission->load(std::memory_order_acquire))
        return Host::ControlError{QStringLiteral("stale-generation")};
    if (requiresAudioDocument(request)
        && (request.audioDocument.isNull() || request.audioDocument != application.documentIncarnation()))
        return Host::ControlError{QStringLiteral("stale-audio-effects-document")};
    if (peer)
    {
        const auto& permits = application.bgmEditPermits();
        const auto issued = permits.constFind(peer->permit);
        if ((issued != permits.cend() && (issued->documentIncarnation != peer->documentIncarnation
                || issued->playlistId != peer->playlist))
            || request.documentAction || request.documentChoice || request.authored || request.runtime
            || request.canvasBoundaryHighlight || request.desktopBackground || request.desktopDeployment || request.identifyBoundDisplay
            || request.openBgmEditor || request.type == QLatin1String(SessionBridge::SnapshotCommand)
            || request.type == QLatin1String("GetRuntimePlaybackSample"))
            return Host::ControlError{QStringLiteral("wrong-bgm-peer-context")};
    }
    if ((request.bgmEditorRead && !peer)
        || (request.bgmAuthored && (!peer || !matchesPeer(*peer, request.bgmAuthored->permit, request.bgmAuthored->target)))
        || (request.bgmRuntime && (!peer || !matchesPeer(*peer, request.bgmRuntime->permit, request.bgmRuntime->target))))
        return Host::ControlError{QStringLiteral("wrong-bgm-peer-context")};
    if (request.openBgmEditor && (admittedDocument.isNull() || admittedDocument != application.documentIncarnation()))
        return Host::ControlError{QStringLiteral("stale-audio-effects-document")};
    return std::nullopt;
}

SessionWebViewReply dispatchSessionWebViewCommand(SessionApplication& application, const SessionReadModel& reader,
    const SessionBridge::Request& request, const QUuid& admittedDocument, std::optional<BgmPeerIdentity> peer)
{
    if (const auto error = validateSessionWebViewCommand(application, reader, request, admittedDocument, peer))
        return Host::ControlReply{*error};

    try
    {
        if (request.documentAction)
            return Host::ControlReply{application.beginDocumentOperation(*request.documentAction)};
        if (request.documentChoice)
            return Host::ControlReply{application.continueDocumentOperation(request.documentToken, *request.documentChoice)};
        if (request.authored)
            return Host::ControlReply{application.applyAuthoredOperation(*request.authored)};
        if (request.runtime)
            return Host::ControlReply{application.applyRuntimeOperation(*request.runtime)};
        if (request.identifyBoundDisplay)
        {
            if (!application.closePending() && !application.closing())
                if (const auto target = application.displaySelection().identifyRequest()) return *target;
            return Host::ControlReply{Platform::DisplayIdentifyStatus::Unresolved};
        }
        if (request.canvasBoundaryHighlight || request.desktopBackground)
        {
            const auto result = request.canvasBoundaryHighlight
                ? application.applyHighlightCanvasBoundary(*request.canvasBoundaryHighlight)
                : application.selectCanvasOutsideMode(*request.desktopBackground);
            using Status = SessionApplication::ViewPreferenceResult;
            switch (result)
            {
            case Status::Applied: return Host::ControlReply{Host::ViewPreferenceResult{QStringLiteral("applied")}};
            case Status::AlreadySatisfied: return Host::ControlReply{Host::ViewPreferenceResult{QStringLiteral("already-satisfied")}};
            case Status::Unavailable: return Host::ControlReply{Host::ViewPreferenceResult{QStringLiteral("unavailable")}};
            }
        }
        if (request.bgmAuthored)
            return Host::ControlReply{application.applyBgmAuthoredOperation(*request.bgmAuthored)};
        if (request.bgmRuntime)
        {
            const auto& intent = *request.bgmRuntime;
            return Host::ControlReply{intent.trackId
                ? application.useBgmTrack(intent.permit, intent.target, *intent.trackId)
                : application.bgmRuntimeState(intent.permit, intent.target)};
        }
        if (request.openBgmEditor)
        {
            if (!application.runtimeCommandsPrepared() || application.busy())
                return Host::ControlReply{Host::OpenBgmEditorResult{
                    QStringLiteral("Native Background Music editor is unavailable or busy.")}};
            const auto playlist = *request.openBgmEditor;
            const auto permit = application.issueBgmEditPermit(playlist);
            if (permit.isNull())
                return Host::ControlReply{Host::OpenBgmEditorResult{
                    QStringLiteral("The captured Playlist cannot be admitted.")}};
            const BgmPeerIdentity identity{permit, playlist, application.documentIncarnation()};
            try { return BgmPeerAdmission{identity, captureBgmPeerHeader(reader, identity)}; }
            catch (...)
            {
                application.revokeBgmEditPermit(permit);
                throw;
            }
        }
        if (request.bgmEditorRead && peer)
            return Host::ControlReply{Host::BgmSnapshotResult{
                reader.bgmEditorSnapshot(peer->permit, peer->playlist, request.bgmEditorItem),
                captureBgmPeerHeader(reader, *peer)}};
        if (request.type == QLatin1String(SessionBridge::SnapshotCommand))
            return Host::ControlReply{Host::MainSnapshotResult{reader.snapshot()}};
        if (request.type == QLatin1String("GetRuntimePlaybackSample"))
            return Host::ControlReply{Host::RuntimeSampleResult{application.runtimePlaybackSnapshot()}};
    }
    catch (...)
    {
        if (request.type == QLatin1String(SessionBridge::SnapshotCommand))
            return Host::ControlReply{Host::ControlError{QStringLiteral("snapshot-unavailable")}};
        if (request.bgmEditorRead)
            return Host::ControlReply{Host::ControlError{QStringLiteral("bgm-editor-unavailable")}};
        if (request.openBgmEditor)
            return Host::ControlReply{Host::OpenBgmEditorResult{
                QStringLiteral("Native Background Music editor is unavailable.")}};
        return Host::ControlReply{Host::ControlError{QStringLiteral("control-command-unavailable")}};
    }
    return Host::ControlReply{Host::ControlError{QStringLiteral("control-command-unavailable")}};
}

SessionWebViewFacts captureSessionWebViewFacts(const SessionApplication& application)
{
    auto runtime = application.runtimePlaybackSnapshot();
    const auto duration = runtime.value("duration").toDouble();
    const auto* scene = application.activeTimelinePreviewScene();
    return {application.documentIncarnation(), std::move(runtime), application.audioEffectsSnapshotAfterDrain(),
        application.activeTimelinePreviewSource(), scene ? std::optional<Domain::Scene>{*scene} : std::nullopt,
        application.workspace().desktopCanvas().aspectRatio, duration, application.currentDocumentDisplayName(),
        application.document().isDirty(application.workspace().authoredRevision()), application.desktopDeploymentProjection()};
}

QJsonObject captureBgmPeerHeader(const SessionReadModel& reader, const BgmPeerIdentity& peer)
{
    return reader.bgmEditorHeader(peer.permit, peer.playlist);
}
}
