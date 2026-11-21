#pragma once

#include "WorkspaceModel.h"

#include <QString>
#include <QJsonObject>
#include <QUuid>
#include <QVector>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace DesktopSceneEngine::Player
{
// B3's finite application boundary. Identity and authored truth remain in
// WorkspaceModel; this request never addresses a row, alias or source path.
enum class AuthoredAction
{
    SelectPlaylist, AddPlaylist, RenamePlaylist, RemovePlaylist, MovePlaylist,
    SetPlaylistEnabled, SetProjectPolicy, SetPlaylistPolicy, SetPlaylistMaster,
    SelectItem, AddMedia, RenameItem, RemoveItem, MoveItem, SetItemEnabled,
    SetImageDuration, ApplyVideoRate, SetItemGain, SetItemPresentation
};

struct ItemPresentationPatch
{
    std::optional<Domain::MediaPresentationMode> mode;
    std::optional<Domain::AspectRatio> customAspect;
    std::optional<double> scale;
    std::optional<double> positionX;
    std::optional<double> positionY;
};

struct AuthoredRequest
{
    AuthoredAction action = AuthoredAction::SelectPlaylist;
    PlaylistId playlistId;
    std::optional<ItemId> itemId;
    QString alias;
    int direction = 0;
    bool enabled = true;
    double value = 0.0;
    PlaybackSequenceMode policy = PlaybackSequenceMode::ListLoop;
    ItemPresentationPatch presentation;
    // Rename/delete dialogs capture the exact prior alias, independent of
    // unrelated revisions. Duplicate aliases remain legal.
    std::optional<QString> expectedAlias;
    std::optional<std::uint64_t> expectedRevision;
};

enum class AuthoredStatus
{
    Accepted, Applied, NoChange, NotFound, WrongParent, InvalidValue, StaleValue,
    StaleRevision, Busy, PickerCancelled, Unavailable,
    RevokedPermit, WrongDocument, TrackNotFound
};
enum class AuthoredOutcome { Committed, Unchanged, Rejected, NotApplicable };
enum class RuntimeOutcome { NotApplicable, Synchronized, Unchanged, Stopped, Warning, Failed, Pending, Cancelled, DeferredUnavailable };
enum class PositionRestoreOutcome { NotApplicable, Restored, ReboundAtStart, Failed };
enum class MediaSourceOutcome { Committed, Unsupported, Rejected };

// Native-only C2 editing context. A page cannot issue its own permit; C4 will
// obtain one from its Session-owned peer lifetime. Runtime Use is not an action.
struct PlaylistBgmTarget
{
    PlaylistId playlistId;
    friend bool operator==(const PlaylistBgmTarget&, const PlaylistBgmTarget&) = default;
};
struct ItemBgmTarget
{
    PlaylistId playlistId;
    ItemId itemId;
    friend bool operator==(const ItemBgmTarget&, const ItemBgmTarget&) = default;
};
using BgmTarget = std::variant<PlaylistBgmTarget, ItemBgmTarget>;
inline PlaylistId bgmPlaylistId(const BgmTarget& target)
{ return std::visit([](const auto& value) { return value.playlistId; }, target); }
inline std::optional<ItemId> bgmItemId(const BgmTarget& target)
{
    if (const auto* item = std::get_if<ItemBgmTarget>(&target)) return item->itemId;
    return std::nullopt;
}
enum class BgmAuthoredAction
{
    SetEnabled, SetQueueMode, SetQueueVolume, SetTrackVolume,
    RemoveTrack, MoveTrack, AddTracks
};
struct BgmAuthoredRequest
{
    BgmAuthoredAction action = BgmAuthoredAction::SetEnabled;
    QUuid permit;
    BgmTarget target;
    std::optional<Domain::TrackId> trackId;
    bool enabled = true;
    Domain::MusicPlaybackMode mode = Domain::MusicPlaybackMode::ListLoop;
    double value = 0.0;
    int direction = 0;
    std::optional<std::uint64_t> expectedRevision;
};
struct BgmAuthoredReadback
{
    std::optional<bool> enabled;
    std::optional<Domain::MusicPlaybackMode> mode;
    std::optional<double> queueVolume;
    std::optional<double> trackVolume;
    // Only reorder/removal return order. Volume replies never copy a queue.
    std::optional<std::vector<Domain::TrackId>> order;
};
enum class BgmImportOutcome { Completed, Cancelled, RejectedBeforePicker, RejectedAtCompletion };
struct BgmSourceResult
{
    QString source;
    std::optional<Domain::TrackId> trackId;
    MediaSourceOutcome outcome = MediaSourceOutcome::Rejected;
};

struct AuthoredWarning
{
    QString code;
    QString message;
};

struct MediaSourceResult
{
    QString source;
    std::optional<ItemId> itemId;
    MediaSourceOutcome outcome = MediaSourceOutcome::Rejected;
    RuntimeOutcome runtime = RuntimeOutcome::NotApplicable;
    QVector<AuthoredWarning> warnings;
};

struct AuthoredResult
{
    // A canonical authored commit may still have a pending native audio effect.
    // The receipt belongs to the native root, independently of page lifetime.
    std::optional<QJsonObject> audioEffect;
    std::optional<RuntimeOutcome> runtimeOnApplied;
    AuthoredStatus status = AuthoredStatus::Unavailable;
    AuthoredOutcome authored = AuthoredOutcome::Rejected;
    RuntimeOutcome runtime = RuntimeOutcome::NotApplicable;
    PositionRestoreOutcome positionRestore = PositionRestoreOutcome::NotApplicable;
    PlaylistId playlistId;
    std::optional<ItemId> itemId;
    QUuid token;
    std::uint64_t authoredRevision = 0;
    bool dirty = false;
    // Narrow authoritative read-back for SetPlaylistMaster, including no-op or
    // rejection of a value on an existing Playlist. Never echo the UI request.
    std::optional<double> playlistMasterVolume;
    QVector<AuthoredWarning> warnings;
    QVector<MediaSourceResult> sources;
    std::optional<BgmTarget> bgmTarget;
    std::optional<Domain::TrackId> bgmTrackId;
    std::optional<BgmAuthoredReadback> bgmReadback;
    std::optional<BgmImportOutcome> bgmImport;
    QVector<BgmSourceResult> bgmSources;
};
}
