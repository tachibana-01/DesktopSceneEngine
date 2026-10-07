#include "PlaybackRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DesktopSceneEngine
{
namespace Runtime
{

void PlaybackRuntime::configure(const Domain::Playback& playback)
{
    configure(playback, 0.0F);
}

void PlaybackRuntime::configure(
    const Domain::Playback& playback,
    float duration
)
{
    configuration_ = playback;
    if (!std::isfinite(configuration_.speed)
        || configuration_.speed < 0.0F)
    {
        configuration_.speed = 0.0F;
    }
    duration_ = std::max(duration, 0.0F);
    if (!std::isfinite(duration_))
    {
        duration_ = 0.0F;
    }
    currentTime_ = 0.0F;
    paused_ = false;
    configured_ = true;
}

void PlaybackRuntime::update(float deltaTime) noexcept
{
    if (!configured_ || paused_ || !std::isfinite(deltaTime)
        || deltaTime <= 0.0F || !std::isfinite(configuration_.speed)
        || configuration_.speed <= 0.0F)
    {
        return;
    }

    const double nextTime = static_cast<double>(currentTime_)
        + static_cast<double>(deltaTime)
            * static_cast<double>(configuration_.speed);
    if (!std::isfinite(nextTime))
    {
        currentTime_ = (std::numeric_limits<float>::max)();
        return;
    }

    currentTime_ = nextTime >= static_cast<double>(
        (std::numeric_limits<float>::max)())
        ? (std::numeric_limits<float>::max)()
        : static_cast<float>(nextTime);
}

void PlaybackRuntime::pause() noexcept
{
    paused_ = true;
}

void PlaybackRuntime::resume() noexcept
{
    paused_ = false;
}

void PlaybackRuntime::togglePause() noexcept
{
    paused_ = !paused_;
}

void PlaybackRuntime::setSpeed(float speed) noexcept
{
    configuration_.speed = std::isfinite(speed) && speed >= 0.0F
        ? speed : 0.0F;
}

bool PlaybackRuntime::isPaused() const noexcept
{
    return paused_;
}

bool PlaybackRuntime::shouldAdvanceScene() const noexcept
{
    return configured_ && !paused_ && std::isfinite(duration_)
        && std::isfinite(currentTime_) && duration_ > 0.0F
        && currentTime_ >= duration_;
}

void PlaybackRuntime::reset() noexcept
{
    configuration_ = Domain::Playback{};
    duration_ = 0.0F;
    currentTime_ = 0.0F;
    paused_ = false;
    configured_ = false;
}

bool PlaybackRuntime::isConfigured() const noexcept
{
    return configured_;
}

const Domain::Playback& PlaybackRuntime::configuration() const noexcept
{
    return configuration_;
}

float PlaybackRuntime::currentTime() const noexcept
{
    return currentTime_;
}

float PlaybackRuntime::duration() const noexcept
{
    return duration_;
}

void PlaybackRuntime::restoreTime(float time) noexcept
{
    currentTime_ = std::isfinite(time) && time >= 0.0F ? time : 0.0F;
}

}
}
