#pragma once

#include <DesktopSceneEngine/Domain/Playback.h>

namespace DesktopSceneEngine
{
namespace Runtime
{

class PlaybackRuntime
{
public:

    void configure(const Domain::Playback& playback);

    void configure(const Domain::Playback& playback, float duration);

    void update(float deltaTime) noexcept;

    void pause() noexcept;

    void resume() noexcept;

    void togglePause() noexcept;

    void setSpeed(float speed) noexcept;

    [[nodiscard]] bool isPaused() const noexcept;

    [[nodiscard]] bool shouldAdvanceScene() const noexcept;

    void reset() noexcept;

    [[nodiscard]] bool isConfigured() const noexcept;

    [[nodiscard]] const Domain::Playback& configuration() const noexcept;

    [[nodiscard]] float currentTime() const noexcept;

    [[nodiscard]] float duration() const noexcept;

    void restoreTime(float time) noexcept;

private:

    Domain::Playback configuration_{};
    float duration_ = 0.0F;
    float currentTime_ = 0.0F;
    bool paused_ = false;
    bool configured_ = false;
};

}
}
