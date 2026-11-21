#pragma once

#include <DesktopSceneEngine/BackgroundMusicControl.h>

#include <optional>

namespace DesktopSceneEngine::BackgroundMusicObservation
{
enum class Scope { PlaylistGlobal, Item };
enum class PlaybackState { Stopped, Playing, Paused };
enum class Phase { SilentEmptyQueue, PendingStart, Playing, NoPlayableTrack, WaitingForExecutor };

// Diagnostic detail only. These enums do not choose retry/progression policy.
enum class FailureStage { Unknown, SourceOpen, ResourceAllocation, MixerRegistration, Playback };
enum class FailureCode
{
    Unknown, OpenFailure, NoAudioStream, DecoderUnavailable, DecodeFailure,
    ResampleFailure, ResourceFailure, AllocationFailure, RegistrationFailure, InputFailed
};

struct FailedAttempt
{
    BackgroundMusicControl::TrackId trackId{};
    FailureStage stage = FailureStage::Unknown;
    FailureCode code = FailureCode::Unknown;
    bool operator==(const FailedAttempt&) const = default;
};

struct ScopeSnapshot
{
    Scope scope = Scope::PlaylistGlobal;
    bool active = false; // The owner identity is active, including a disabled queue.
    bool exists = false; // An actual native music session exists for that owner.
    std::optional<Phase> phase{};
    std::optional<BackgroundMusicControl::TrackId> currentTrackId{};
    // Target-independent runtime capability, never final Session Use admission.
    bool switchEligible = false;
    std::optional<FailedAttempt> lastFailedAttempt{};
    bool operator==(const ScopeSnapshot&) const = default;
};

// Detached values only: no editor/config/dirty state, source objects or handles.
// Read on the existing Engine/control thread; no audio-thread observer is added.
struct Snapshot
{
    PlaybackState playbackState = PlaybackState::Stopped;
    std::optional<BackgroundMusicControl::PlaylistId> activePlaylistId{};
    std::optional<BackgroundMusicControl::ItemId> activeItemId{};
    ScopeSnapshot playlist{};
    ScopeSnapshot item{Scope::Item};
    bool operator==(const Snapshot&) const = default;
};
}
