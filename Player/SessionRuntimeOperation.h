#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include "SessionAuthoredOperation.h"
#include <DesktopSceneEngine/BackgroundMusicObservation.h>

namespace DesktopSceneEngine::Player
{

enum class RuntimeAction { PlayPlaylist, PlayItem, PlayPause, Previous, Next, Seek, ToggleOutputMute };

struct RuntimeRequest
{
    RuntimeAction action;
    QUuid playlistId, itemId;
    QString bindingToken;
    int value = 0;
    double duration = 0;
};

struct RuntimeResult
{
    QString status, message;
    QJsonObject sample;
    std::optional<QJsonObject> audioEffect;
};

// C3 values are detached native runtime facts, not another queue/runtime owner.
enum class BgmRuntimeCommandStatus { Rejected, Ready, NoChange, Admitted };
enum class BgmRuntimeRejectReason
{
    None, Unavailable, RevokedPermit, Busy, TargetMissing, WrongParent,
    TrackMissing, Disabled, Inactive, NotSwitchable, ControlRejected
};
struct BgmRuntimeState
{
    BgmTarget target;
    BackgroundMusicObservation::PlaybackState playbackState = BackgroundMusicObservation::PlaybackState::Stopped;
    std::optional<BackgroundMusicControl::PlaylistId> activePlaylistId;
    std::optional<BackgroundMusicControl::ItemId> activeItemId;
    BackgroundMusicObservation::ScopeSnapshot scope;
    bool operator==(const BgmRuntimeState&) const = default;
};
struct BgmRuntimeResult
{
    BgmRuntimeCommandStatus status = BgmRuntimeCommandStatus::Rejected;
    BgmRuntimeRejectReason reason = BgmRuntimeRejectReason::Unavailable;
    std::optional<BgmRuntimeState> state;
    std::optional<QJsonObject> audioEffect;
};
struct BgmRuntimeRequest
{
    QUuid permit;
    BgmTarget target;
    std::optional<Domain::TrackId> trackId; // Present only for Use, never assigned Current by transport.
};

struct TimelineHoverRequest
{
    QString bindingToken;
    QUuid itemId;
    quint32 serial = 0;
    double time = 0;
};

struct TimelineHoverResult
{
    QString status, bindingToken;
    QUuid itemId;
    quint32 serial = 0;
    double time = 0;
    QImage image;
};

}
