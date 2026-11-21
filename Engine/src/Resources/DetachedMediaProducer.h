#pragma once

#include "MediaResource.h"

#include <DesktopSceneEngine/EnginePresentation.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace DesktopSceneEngine::Resources
{

// Used by both the inline compatibility path and the detached producer.
[[nodiscard]] std::unique_ptr<MediaResource> openPlaybackResource(
    const std::filesystem::path& path, const std::atomic_bool* cancel = nullptr);

class DetachedMediaProducer final
{
public:
    using Token = std::uint64_t;
    enum class State { Pending, Ready, Failed, Expired };
    enum class FrameState { Ready, EndOfStream, Failed };

    struct Prepared
    {
        MediaType type = MediaType::Unknown;
        float duration = 0;
        float position = 0;
        double timestamp = -1;
        std::shared_ptr<const PresentationFrame> frame;
    };

    struct Frame
    {
        Token binding = 0;
        float position = 0;
        double timestamp = -1;
        FrameState state = FrameState::Failed;
        std::shared_ptr<const PresentationFrame> pixels;
    };

    // The test callback runs after Ready has been published, without the
    // mailbox lock. Production never supplies it.
    using PreparationPublishedForTesting = std::function<void(Token)>;
    explicit DetachedMediaProducer(void* controlWakeEvent, PreparationPublishedForTesting publishedForTesting = {});
    ~DetachedMediaProducer();
    DetachedMediaProducer(const DetachedMediaProducer&) = delete;
    DetachedMediaProducer& operator=(const DetachedMediaProducer&) = delete;

    [[nodiscard]] bool prepareScene(Token token, std::optional<std::filesystem::path> path,
        float declaredDuration, float initialPosition);
    [[nodiscard]] bool prepareSeek(Token token, Token binding, float position, float duration);
    [[nodiscard]] State preparedState(Token token) const noexcept;
    // Returns only the exact ready request. Promote will install the staged
    // codec on the resource owner before the next playback frame request.
    [[nodiscard]] std::optional<Prepared> commit(Token token, bool promote) noexcept;
    void cancel(Token token) noexcept;

    [[nodiscard]] bool requestFrame(Token binding, float position) noexcept;
    [[nodiscard]] std::optional<Frame> takeFrame() noexcept;
    // Retire the predecessor of this accepted binding only.
    void releasePreviousBinding(Token acceptedBinding) noexcept;
    void restorePreviousBinding(Token incumbent) noexcept;
    void clearBinding() noexcept;

    void beginShutdown() noexcept;
    [[nodiscard]] bool shutdownComplete() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
