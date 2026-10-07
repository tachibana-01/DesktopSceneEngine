#include "PlaybackRuntime.h"

#include <DesktopSceneEngine/Domain/Playback.h>

#include <cmath>
#include <iostream>
#include <limits>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool nearlyEqual(float actual, float expected) noexcept
{
    constexpr float tolerance = 0.0001F;
    return (actual - expected < tolerance) && (expected - actual < tolerance);
}

bool verifyConfiguration(
    const DesktopSceneEngine::Domain::Playback& actual,
    const DesktopSceneEngine::Domain::Playback& expected
)
{
    return require(actual.loop == expected.loop, "Playback loop should match")
        && require(nearlyEqual(actual.speed, expected.speed), "Playback speed should match")
        && require(nearlyEqual(actual.volume, expected.volume), "Playback volume should match");
}

}

int main()
{
    DesktopSceneEngine::Runtime::PlaybackRuntime runtime{};

    if (!require(!runtime.isConfigured(), "PlaybackRuntime should start unconfigured"))
    {
        return 1;
    }

    const DesktopSceneEngine::Domain::Playback firstConfiguration{
        false,
        1.5F,
        0.65F
    };
    runtime.configure(firstConfiguration);

    if (!require(runtime.isConfigured(), "PlaybackRuntime should become configured"))
    {
        return 1;
    }

    if (!verifyConfiguration(runtime.configuration(), firstConfiguration))
    {
        return 1;
    }

    const DesktopSceneEngine::Domain::Playback secondConfiguration{
        true,
        0.75F,
        0.25F
    };
    runtime.configure(secondConfiguration);

    if (!verifyConfiguration(runtime.configuration(), secondConfiguration))
    {
        return 1;
    }

    runtime.reset();
    if (!require(!runtime.isConfigured(), "PlaybackRuntime should reset configuration state"))
    {
        return 1;
    }

    runtime.reset();
    if (!require(!runtime.isConfigured(), "Repeated PlaybackRuntime reset should be safe"))
    {
        return 1;
    }

    const DesktopSceneEngine::Domain::Playback timedConfiguration{
        false,
        1.0F,
        1.0F
    };
    runtime.configure(timedConfiguration, 1.0F);
    runtime.update(0.5F);

    if (!require(
            !runtime.shouldAdvanceScene(),
            "Playback should not transition before its duration"))
    {
        return 1;
    }

    runtime.update(0.6F);
    if (!require(
            runtime.shouldAdvanceScene(),
            "Playback should request a transition after its duration"))
    {
        return 1;
    }

    runtime.pause();
    const float pausedTime = runtime.currentTime();
    runtime.update(10.0F);
    if (!require(
            runtime.isPaused() && nearlyEqual(runtime.currentTime(), pausedTime),
            "Paused playback should not advance"))
    {
        return 1;
    }

    runtime.resume();
    runtime.configure(timedConfiguration, 1.0F);
    runtime.update(1.0F);
    if (!require(
            runtime.shouldAdvanceScene(),
            "Resumed playback should advance normally"))
    {
        return 1;
    }

    runtime.restoreTime(0.25F);
    if (!require(
        nearlyEqual(runtime.currentTime(), 0.25F),
        "Playback time should be restorable after a failed transition"))
    {
        return 1;
    }

    runtime.configure(timedConfiguration, 10.0F);
    runtime.update(1.0F);
    if (!require(
            nearlyEqual(runtime.currentTime(), 1.0F),
            "Normal playback should advance at 1x"))
    {
        return 1;
    }

    const DesktopSceneEngine::Domain::Playback halfSpeed{
        false,
        0.5F,
        1.0F
    };
    runtime.configure(halfSpeed, 10.0F);
    runtime.update(2.0F);
    if (!require(
            nearlyEqual(runtime.currentTime(), 1.0F),
            "Half-speed playback should advance at 0.5x"))
    {
        return 1;
    }

    const DesktopSceneEngine::Domain::Playback doubleSpeed{
        false,
        2.0F,
        1.0F
    };
    runtime.configure(doubleSpeed, 10.0F);
    runtime.update(1.0F);
    if (!require(
        nearlyEqual(runtime.currentTime(), 2.0F),
        "Double-speed playback should advance at 2x"))
    {
        return 1;
    }

    runtime.setSpeed(std::numeric_limits<float>::quiet_NaN());
    runtime.update(1.0F);
    if (!require(nearlyEqual(runtime.currentTime(), 2.0F),
            "Non-finite speed must not corrupt playback time"))
    {
        return 1;
    }

    runtime.setSpeed(std::numeric_limits<float>::infinity());
    runtime.update(1.0F);
    return require(nearlyEqual(runtime.currentTime(), 2.0F),
        "Infinite speed must not corrupt playback time") ? 0 : 1;
}
