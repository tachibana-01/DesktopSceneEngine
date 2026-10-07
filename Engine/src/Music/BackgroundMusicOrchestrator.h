#pragma once

#include "BackgroundMusicRuntime.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

namespace DesktopSceneEngine::Music
{

using MusicActionExecutor = std::function<std::optional<MusicRuntimeEvent>(
    const MusicRuntimeAction&)>;

enum class MusicProcessResult
{
    Completed,
    Enqueued,
    SafetyLimitExceeded
};

// Engine/control-thread only. This type is intentionally not thread-safe and
// must not be called directly from a real-time audio callback.
class BackgroundMusicOrchestrator final
{
public:
    BackgroundMusicOrchestrator(
        BackgroundMusicRuntime& runtime,
        MusicActionExecutor executor
    );

    [[nodiscard]] MusicProcessResult processMusicEvent(MusicRuntimeEvent event);

private:
    BackgroundMusicRuntime& runtime_;
    MusicActionExecutor executor_;
    std::deque<MusicRuntimeEvent> events_{};
    std::vector<MusicRuntimeEvent> reentrantEvents_{};
    bool processing_ = false;
};

}
