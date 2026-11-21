#include "DetachedMediaProducer.h"

#include "ImageResource.h"
#include "VideoResource.h"
#include "../Rendering/GraphicsBackend.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace DesktopSceneEngine::Resources
{
namespace
{
bool isVideo(const std::filesystem::path& path)
{
    auto extension = path.extension().wstring();
    for (auto& ch : extension)
        if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
    return extension == L".mp4" || extension == L".avi" || extension == L".mov"
        || extension == L".mkv" || extension == L".webm" || extension == L".video";
}

float clampPosition(float position, float duration) noexcept
{
    if (position <= 0 || duration <= 0) return 0;
    return (std::min)(position, std::nextafter(duration, 0.0F));
}

std::shared_ptr<const PresentationFrame> seal(PresentationFrameKind kind,
    const void* data, int width, int height, int channels, std::size_t stride, std::size_t size)
{
    if (!data || !Rendering::Detail::hasValidRgbaBuffer(width, height, channels, stride, size))
        return {};
    auto result = std::make_shared<PresentationFrame>();
    result->kind = kind;
    result->width = width;
    result->height = height;
    result->rowStride = stride;
    result->pixels.resize(stride * static_cast<std::size_t>(height));
    std::memcpy(result->pixels.data(), data, result->pixels.size());
    return result;
}

double presentedTime(const DecodedFrame& frame, double fallback) noexcept
{
    return frame.hasTimestamp && std::isfinite(frame.timestampSeconds) && frame.timestampSeconds >= 0
        ? frame.timestampSeconds : fallback;
}

DetachedMediaProducer::Frame decodeFrame(VideoResource& video,
    DetachedMediaProducer::Token binding, float position, const std::atomic_bool& cancel)
{
    using FrameState = DetachedMediaProducer::FrameState;
    DetachedMediaProducer::Frame result{binding, position};
    const auto status = video.frameAt(position, VideoFrameDecodeOptions{0, 0, &cancel});
    const auto& frame = video.currentFrame();
    if (status != VideoFrameDecodeStatus::Ready)
    {
        if (status == VideoFrameDecodeStatus::Unavailable && video.lastFrameAtReachedEndOfStream()
            && frame.data && frame.width > 0 && frame.height > 0 && frame.channels > 0
            && frame.rowStride > 0 && frame.byteSize > 0 && video.verifyBufferGuard("at-playback-end-of-stream"))
        {
            result.timestamp = presentedTime(frame, -1);
            result.state = FrameState::EndOfStream;
        }
        return result;
    }
    if (!video.verifyBufferGuard("before-detached-frame-copy")) return result;
    result.pixels = seal(PresentationFrameKind::Video, frame.data, frame.width, frame.height,
        frame.channels, frame.rowStride, frame.byteSize);
    if (!result.pixels || !video.verifyBufferGuard("after-detached-frame-copy"))
    {
        result.pixels.reset();
        return result;
    }
    result.timestamp = presentedTime(frame, position);
    result.state = FrameState::Ready;
    return result;
}
}

std::unique_ptr<MediaResource> openPlaybackResource(
    const std::filesystem::path& path, const std::atomic_bool* cancel)
{
    if (cancel && cancel->load(std::memory_order_acquire)) return {};
    if (isVideo(path))
    {
        auto video = std::make_unique<VideoResource>();
        return video->open(path, cancel) ? std::move(video) : nullptr;
    }
    auto image = std::make_unique<ImageResource>();
    return image->loadFromFile(path) ? std::move(image) : nullptr;
}

struct DetachedMediaProducer::Impl
{
    struct Preparation
    {
        Token token = 0;
        Token seekBinding = 0;
        std::optional<std::filesystem::path> path;
        float duration = 0;
        float position = 0;
        std::shared_ptr<std::atomic_bool> cancel;
    };

    struct Playback
    {
        Token binding;
        float position;
    };

    explicit Impl(void* event, PreparationPublishedForTesting published)
        : controlWakeEvent(event), publishedForTesting(std::move(published)), thread([this] { run(); })
    {}

    ~Impl()
    {
        stop();
        thread.join();
    }

    void notifyControl() const noexcept
    {
#ifdef _WIN32
        if (controlWakeEvent) SetEvent(static_cast<HANDLE>(controlWakeEvent));
#endif
    }

    bool prepare(Preparation request)
    {
        auto cancel = std::make_shared<std::atomic_bool>(false);
        std::lock_guard lock(mutex);
        if (stopping || currentToken || request.token == 0) return false;
        request.cancel = cancel;
        currentToken = request.token;
        currentCancel = std::move(cancel);
        currentState = State::Pending;
        pendingPreparation = std::move(request);
        pendingFrame.reset();
        completedFrame.reset();
        cancelFrame.store(true, std::memory_order_release);
        changed.notify_one();
        return true;
    }

    void resetPreparation() noexcept
    {
        currentToken = 0;
        currentCancel.reset();
        currentState = State::Expired;
        pendingPreparation.reset();
        prepared.reset();
    }

    void stop() noexcept
    {
        std::lock_guard lock(mutex);
        stopping = true;
        if (currentCancel) currentCancel->store(true, std::memory_order_release);
        cancelFrame.store(true, std::memory_order_release);
        resetPreparation();
        pendingFrame.reset();
        changed.notify_one();
    }

    void makePrepared(const Preparation& request,
        std::unique_ptr<MediaResource>& candidate, Token& candidateToken,
        MediaResource* active, Token activeToken)
    {
        std::unique_ptr<MediaResource> resource;
        std::optional<Prepared> result;
        try
        {
            if (request.seekBinding)
            {
                auto* video = dynamic_cast<VideoResource*>(active);
                if (video && request.seekBinding == activeToken)
                {
                    const auto frame = decodeFrame(*video, activeToken, request.position, *request.cancel);
                    if (frame.state == FrameState::Ready)
                        result = Prepared{MediaType::Video, request.duration, request.position,
                            frame.timestamp, frame.pixels};
                }
            }
            else if (!request.path)
                result = Prepared{MediaType::Unknown, request.duration,
                    clampPosition(request.position, request.duration), -1, {}};
            else if ((resource = openPlaybackResource(*request.path, request.cancel.get())))
            {
                if (auto* video = dynamic_cast<VideoResource*>(resource.get()))
                {
                    const auto duration = video->duration();
                    const auto position = clampPosition(request.position, duration);
                    const auto frame = decodeFrame(*video, request.token, position, *request.cancel);
                    if (frame.state == FrameState::Ready && std::isfinite(duration) && duration > 0)
                        result = Prepared{MediaType::Video, duration, position, frame.timestamp, frame.pixels};
                }
                else if (auto* image = dynamic_cast<ImageResource*>(resource.get()); image
                    && std::isfinite(request.duration) && request.duration >= 0)
                {
                    auto frame = seal(PresentationFrameKind::Image, image->data(), image->width(), image->height(),
                        image->channels(), image->rowStride(), image->byteSize());
                    if (frame)
                        result = Prepared{MediaType::Image, request.duration,
                            clampPosition(request.position, request.duration), -1, std::move(frame)};
                    // Immutable pixels carry image content after preparation.
                    resource.reset();
                }
            }
        }
        catch (const std::exception&)
        {
            result.reset();
        }

        const bool ready = result.has_value();
        {
            std::lock_guard lock(mutex);
            if (currentToken != request.token || request.cancel->load(std::memory_order_acquire)) return;
            if (ready && !request.seekBinding)
            {
                candidate = std::move(resource);
                candidateToken = request.token;
            }
            prepared = std::move(result);
            currentState = ready ? State::Ready : State::Failed;
            notifyControl();
        }
        if (ready && publishedForTesting) publishedForTesting(request.token);
    }

    void run() noexcept
    {
        {
            // The resource executor alone owns and destroys the codecs.
            std::unique_ptr<MediaResource> active, candidate, previous;
            Token activeToken = 0;
            Token candidateToken = 0;
            Token previousToken = 0;
            while (true)
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] {
                    return stopping || clearActive || promoteToken || discardToken || restoreToken || releasePreviousToken
                        || pendingPreparation || pendingFrame;
                });
                if (stopping) break;

                if (std::exchange(clearActive, false))
                {
                    activeToken = 0;
                    previousToken = 0;
                    lock.unlock();
                    active.reset();
                    previous.reset();
                    continue;
                }
                if (const auto token = std::exchange(promoteToken, 0))
                {
                    lock.unlock();
                    if (candidateToken == token)
                    {
                        previous = std::move(active);
                        previousToken = activeToken;
                        activeToken = token;
                        active = std::move(candidate);
                        candidateToken = 0;
                    }
                    continue;
                }
                if (const auto token = std::exchange(restoreToken, 0))
                {
                    lock.unlock();
                    if (previousToken == token)
                    {
                        active = std::move(previous);
                        activeToken = previousToken;
                        previousToken = 0;
                    }
                    continue;
                }
                if (const auto token = std::exchange(releasePreviousToken, 0))
                {
                    lock.unlock();
                    if (activeToken == token)
                    {
                        previousToken = 0;
                        previous.reset();
                    }
                    continue;
                }
                if (const auto token = std::exchange(discardToken, 0))
                {
                    lock.unlock();
                    if (candidateToken == token)
                    {
                        candidate.reset();
                        candidateToken = 0;
                    }
                    continue;
                }
                if (pendingPreparation)
                {
                    auto request = std::move(*pendingPreparation);
                    pendingPreparation.reset();
                    lock.unlock();
                    makePrepared(request, candidate, candidateToken, active.get(), activeToken);
                    continue;
                }

                const auto request = *pendingFrame;
                pendingFrame.reset();
                frameRunning = true;
                cancelFrame.store(false, std::memory_order_release);
                lock.unlock();

                Frame result{request.binding, request.position};
                if (auto* video = dynamic_cast<VideoResource*>(active.get()); video && request.binding == activeToken)
                {
                    try { result = decodeFrame(*video, activeToken, request.position, cancelFrame); }
                    catch (const std::exception&) {}
                }
                lock.lock();
                frameRunning = false;
                if (!cancelFrame.load(std::memory_order_acquire))
                {
                    completedFrame = std::move(result);
                    notifyControl();
                }
            }
        }
        stopped.store(true, std::memory_order_release);
        notifyControl();
    }

    void* const controlWakeEvent;
    const PreparationPublishedForTesting publishedForTesting;
    mutable std::mutex mutex;
    std::condition_variable changed;
    Token currentToken = 0;
    Token promoteToken = 0;
    Token discardToken = 0;
    Token restoreToken = 0;
    Token releasePreviousToken = 0;
    State currentState = State::Expired;
    std::shared_ptr<std::atomic_bool> currentCancel;
    std::optional<Preparation> pendingPreparation;
    std::optional<Prepared> prepared;
    std::optional<Playback> pendingFrame;
    std::optional<Frame> completedFrame;
    std::atomic_bool cancelFrame{false};
    std::atomic_bool stopped{false};
    bool clearActive = false;
    bool frameRunning = false;
    bool stopping = false;
    std::thread thread;
};

DetachedMediaProducer::DetachedMediaProducer(void* event, PreparationPublishedForTesting published)
    : impl_(std::make_unique<Impl>(event, std::move(published)))
{}
DetachedMediaProducer::~DetachedMediaProducer() = default;

bool DetachedMediaProducer::prepareScene(Token token, std::optional<std::filesystem::path> path,
    float duration, float position)
{
    return impl_->prepare({token, 0, std::move(path), duration, position, {}});
}

bool DetachedMediaProducer::prepareSeek(Token token, Token binding, float position, float duration)
{
    return impl_->prepare({token, binding, {}, duration, position, {}});
}

DetachedMediaProducer::State DetachedMediaProducer::preparedState(Token token) const noexcept
{
    std::lock_guard lock(impl_->mutex);
    return token != 0 && impl_->currentToken == token ? impl_->currentState : State::Expired;
}

std::optional<DetachedMediaProducer::Prepared> DetachedMediaProducer::commit(Token token, bool promote) noexcept
{
    std::lock_guard lock(impl_->mutex);
    if (!token || impl_->currentToken != token || !impl_->prepared) return {};
    auto result = std::move(impl_->prepared);
    impl_->resetPreparation();
    if (promote)
    {
        impl_->promoteToken = token;
        impl_->changed.notify_one();
    }
    // A previously running playback job cannot publish after a new binding
    // or seek commits, even if it returns at the same instant.
    impl_->pendingFrame.reset();
    impl_->completedFrame.reset();
    return result;
}

void DetachedMediaProducer::cancel(Token token) noexcept
{
    std::lock_guard lock(impl_->mutex);
    if (impl_->currentToken != token || token == 0) return;
    impl_->currentCancel->store(true, std::memory_order_release);
    impl_->resetPreparation();
    impl_->discardToken = token;
    impl_->changed.notify_one();
}

bool DetachedMediaProducer::requestFrame(Token binding, float position) noexcept
{
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping || impl_->frameRunning || impl_->pendingFrame || impl_->completedFrame
        || impl_->currentToken) return false;
    impl_->pendingFrame = Impl::Playback{binding, position};
    impl_->changed.notify_one();
    return true;
}

std::optional<DetachedMediaProducer::Frame> DetachedMediaProducer::takeFrame() noexcept
{
    std::lock_guard lock(impl_->mutex);
    return std::exchange(impl_->completedFrame, std::nullopt);
}

void DetachedMediaProducer::releasePreviousBinding(Token acceptedBinding) noexcept
{
    if (!acceptedBinding) return;
    std::lock_guard lock(impl_->mutex);
    impl_->releasePreviousToken = acceptedBinding;
    impl_->changed.notify_one();
}

void DetachedMediaProducer::restorePreviousBinding(Token incumbent) noexcept
{
    std::lock_guard lock(impl_->mutex);
    impl_->restoreToken = incumbent;
    impl_->pendingFrame.reset();
    impl_->completedFrame.reset();
    impl_->cancelFrame.store(true, std::memory_order_release);
    impl_->changed.notify_one();
}

void DetachedMediaProducer::clearBinding() noexcept
{
    std::lock_guard lock(impl_->mutex);
    impl_->clearActive = true;
    if (impl_->promoteToken) impl_->discardToken = impl_->promoteToken;
    impl_->promoteToken = 0;
    impl_->restoreToken = 0;
    impl_->releasePreviousToken = 0;
    impl_->pendingFrame.reset();
    impl_->completedFrame.reset();
    impl_->cancelFrame.store(true, std::memory_order_release);
    impl_->changed.notify_one();
}

void DetachedMediaProducer::beginShutdown() noexcept { impl_->stop(); }
bool DetachedMediaProducer::shutdownComplete() const noexcept
{ return impl_->stopped.load(std::memory_order_acquire); }

}
