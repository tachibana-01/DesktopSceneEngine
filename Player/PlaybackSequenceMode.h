#pragma once

namespace DesktopSceneEngine::Player
{

// Visual main-playback policy only. This intentionally remains separate from
// MusicPlaybackMode, which configures background-music queues.
enum class PlaybackSequenceMode
{
    ListLoop,
    Random,
    RepeatCurrent
};

[[nodiscard]] constexpr bool isPlaybackSequenceMode(PlaybackSequenceMode mode) noexcept
{
    switch (mode)
    {
    case PlaybackSequenceMode::ListLoop:
    case PlaybackSequenceMode::Random:
    case PlaybackSequenceMode::RepeatCurrent:
        return true;
    }

    return false;
}

}
