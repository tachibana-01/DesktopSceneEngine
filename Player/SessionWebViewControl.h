#pragma once

#include "DesktopDeployment.h"
#include "DisplaySelectionState.h"
#include <DesktopSceneEngine/Domain/Scene.h>
#include "TimelinePreviewSource.h"
#include "WebViewSessionHost.h"

namespace DesktopSceneEngine::Player
{
class SessionApplication;
class SessionReadModel;

// Issued by the Session control owner and captured for the life of one peer.
// A transport request cannot select a different peer or change its identity.
struct BgmPeerIdentity
{
    QUuid permit;
    PlaylistId playlist;
    QUuid documentIncarnation;
    bool operator==(const BgmPeerIdentity&) const = default;
};

struct BgmPeerAdmission
{
    BgmPeerIdentity identity;
    QJsonObject header;
};

// A permit reservation is not a successful OpenBackgroundMusicEditor reply.
// The GUI either installs the peer and settles its reply, or revokes the
// unused permit on control when the page/window is no longer current.
using SessionWebViewReply = std::variant<WebViewSessionHost::ControlReply, BgmPeerAdmission, DisplayIdentifyRequest>;

struct SessionWebViewFacts
{
    QUuid documentIncarnation;
    QJsonObject runtime;
    QJsonObject audioEffects;
    std::optional<TimelinePreviewSource> hoverSource;
    std::optional<Domain::Scene> hoverScene;
    Domain::AspectRatio canvasAspect;
    double hoverDuration = 0.0;
    QString documentDisplayName;
    bool dirty = false;
    DesktopDeploymentProjection desktop;
};

// These functions run on the same control executor as application and reader.
// admittedDocument is the GUI's captured document for the v1 Open-BGM request,
// which does not carry an audio-effects context on the wire.
[[nodiscard]] std::optional<WebViewSessionHost::ControlError> validateSessionWebViewCommand(
    const SessionApplication& application, const SessionReadModel& reader,
    const SessionBridge::Request& request, const QUuid& admittedDocument,
    const std::optional<BgmPeerIdentity>& peer);
[[nodiscard]] SessionWebViewReply dispatchSessionWebViewCommand(
    SessionApplication& application, const SessionReadModel& reader,
    const SessionBridge::Request& request, const QUuid& admittedDocument,
    std::optional<BgmPeerIdentity> peer = {});

[[nodiscard]] SessionWebViewFacts captureSessionWebViewFacts(const SessionApplication& application);
[[nodiscard]] QJsonObject captureBgmPeerHeader(const SessionReadModel& reader, const BgmPeerIdentity& peer);
}
