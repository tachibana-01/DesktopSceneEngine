#include <DesktopSceneEngine/Domain/EntityId.h>
#include <DesktopSceneEngine/Domain/Library.h>
#include <DesktopSceneEngine/Domain/Media.h>
#include <DesktopSceneEngine/Domain/Playback.h>
#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/Domain/Scene.h>
#include <DesktopSceneEngine/Engine.h>

#include "Resources/VideoResource.h"
#include "Resources/DetachedMediaProducer.h"
#include "Core/AudioExecutionEngineFixture.h"

#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace
{

bool require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "Test failed: " << message << '\n';
    }
    return condition;
}

std::string word(std::uint32_t value)
{
    std::string result;
    for (unsigned shift = 0; shift < 32; shift += 8)
    {
        result.push_back(static_cast<char>((value >> shift) & 0xffU));
    }
    return result;
}

std::string chunk(const char* tag, const std::string& content)
{
    std::string result{tag, 4};
    result += word(static_cast<std::uint32_t>(content.size()));
    result += content;
    if (content.size() % 2U != 0U)
    {
        result.push_back('\0');
    }
    return result;
}

bool writeTestAvi(const std::filesystem::path& path,
    bool invalidSecondPacket = false, unsigned width = 64)
{
    // Match the native B4 fixture: 12 seconds of 10 fps raw DIB and 8 kHz PCM.
    // Its last video PTS is 11.9 seconds, before the declared 12 second duration.
    constexpr unsigned height = 36, fps = 10, frameCount = 120;
    constexpr unsigned samplesPerFrame = 800;
    const unsigned frameBytes = width * height * 3;
    std::string main;
    for (const auto value : {100000U, (frameBytes + samplesPerFrame * 2) * fps,
        0U, 0x10U, frameCount, 0U, 2U, frameBytes, width, height, 0U, 0U, 0U, 0U})
    {
        main += word(value);
    }
    std::string video{"vidsDIB ", 8};
    for (const auto value : {0U, 0U, 0U, 1U, fps, 0U, frameCount,
        frameBytes, 0xffffffffU, 0U})
    {
        video += word(value);
    }
    video += std::string(4, '\0');
    video.push_back(static_cast<char>(width));
    video.push_back('\0');
    video.push_back(static_cast<char>(height));
    video.push_back('\0');
    std::string format;
    for (const auto value : {40U, width, height, 0x00180001U, 0U,
        frameBytes, 0U, 0U, 0U, 0U})
    {
        format += word(value);
    }
    std::string audio{"auds", 4};
    audio += word(0);
    for (const auto value : {0U, 0U, 0U, 1U, 8000U, 0U,
        frameCount * samplesPerFrame, samplesPerFrame * 2, 0xffffffffU, 2U})
    {
        audio += word(value);
    }
    audio += std::string(8, '\0');
    const std::string pcm{"\x01\x00\x01\x00\x40\x1f\x00\x00"
        "\x80\x3e\x00\x00\x02\x00\x10\x00", 16};
    const std::string header = chunk("LIST", std::string{"hdrl", 4}
        + chunk("avih", main)
        + chunk("LIST", std::string{"strl", 4} + chunk("strh", video)
            + chunk("strf", format))
        + chunk("LIST", std::string{"strl", 4} + chunk("strh", audio)
            + chunk("strf", pcm)));
    std::string frames{"movi", 4}, index;
    const auto append = [&](const char* tag, const std::string& content)
    {
        index += std::string{tag, 4} + word(0x10)
            + word(static_cast<std::uint32_t>(frames.size()))
            + word(static_cast<std::uint32_t>(content.size()));
        frames += chunk(tag, content);
    };
    for (unsigned frame = 0; frame < frameCount; ++frame)
    {
        std::string pixels(frameBytes, '\0');
        for (unsigned y = 0; y < height; ++y)
        {
            for (unsigned x = 0; x < width; ++x)
            {
                const auto offset = (y * width + x) * 3;
                pixels[offset] = static_cast<char>((x * 4 + frame * 3) % 256);
                pixels[offset + 1] = static_cast<char>((y * 7 + frame * 5) % 256);
                pixels[offset + 2] = static_cast<char>((frame * 13) % 256);
            }
        }
        append("00db", invalidSecondPacket && frame == 1
            ? pixels.substr(0, 10) : pixels);
        std::string tone;
        for (unsigned sample = 0; sample < samplesPerFrame; ++sample)
        {
            const auto value = static_cast<std::int16_t>(1800.0 * std::sin(
                6.283185307179586 * 440.0
                * (static_cast<double>(frame) * samplesPerFrame + sample) / 8000.0));
            tone.push_back(static_cast<char>(value & 0xff));
            tone.push_back(static_cast<char>((value >> 8) & 0xff));
        }
        append("01wb", tone);
    }
    const std::string body = std::string{"AVI ", 4} + header
        + chunk("LIST", frames) + chunk("idx1", index);
    const std::string data = chunk("RIFF", body);
    std::ofstream output{path, std::ios::binary};
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    return output.good();
}

struct TestMedia
{
    std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("DesktopSceneEngine-Eof-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = directory / "owned-video.avi";
    std::filesystem::path invalidPath = directory / "invalid-packet.avi";
    std::filesystem::path decodeLayoutPath = directory / "decode-layout.avi";

    ~TestMedia()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(invalidPath, ignored);
        std::filesystem::remove(decodeLayoutPath, ignored);
        std::filesystem::remove(directory, ignored);
    }
};

bool testDecodeLayoutIgnoresDiagnosticEnvironment(const std::filesystem::path& path)
{
    using DesktopSceneEngine::Resources::VideoResource;
    // 68 × 4 = 272 visible bytes per row, so the legacy alignment override
    // would change the 64-byte-aligned production stride from 320 to 272.
    constexpr int Width = 68;
    constexpr std::size_t VisibleRowBytes = static_cast<std::size_t>(Width) * 4;
    constexpr std::size_t ExpectedStride = 320;
    const auto decode = [&path](std::string& visiblePixels)
    {
        VideoResource video;
        if (!require(video.open(path) && video.frameAt(0.0F),
                "The generated decode layout fixture should decode"))
        {
            return false;
        }
        const auto& frame = video.currentFrame();
        if (!require(frame.width == Width && frame.height == 36 && frame.channels == 4
                && frame.data != nullptr && frame.rowStride == ExpectedStride
                && frame.byteSize == ExpectedStride * 36,
                "RGBA geometry and row stride should use the fixed production layout")
            || !require(video.verifyBufferGuard("decode-environment-regression"),
                "The destination buffer guard should remain intact"))
        {
            return false;
        }
        const auto* pixels = static_cast<const char*>(frame.data);
        for (int row = 0; row < frame.height; ++row)
        {
            visiblePixels.append(
                pixels + static_cast<std::size_t>(row) * frame.rowStride, VisibleRowBytes);
        }
        return true;
    };

    std::string baselinePixels, overridePixels;
    return require(writeTestAvi(path, false, Width), "The decode layout fixture should be written")
        && require(_putenv_s("DSE_VIDEO_ALIGNMENT", "") == 0
                && _putenv_s("DSE_VIDEO_PADDING_BYTES", "") == 0,
            "The baseline test process should clear the old variables")
        && decode(baselinePixels)
        && require(_putenv_s("DSE_VIDEO_ALIGNMENT", "1") == 0
                && _putenv_s("DSE_VIDEO_PADDING_BYTES", "4096") == 0,
            "The test process should set values the legacy decoder would honor")
        && decode(overridePixels)
        && require(overridePixels == baselinePixels,
            "The old diagnostic environment should not change decoded pixels");
}

DesktopSceneEngine::Domain::Project makeProject(const std::filesystem::path& path)
{
    using namespace DesktopSceneEngine::Domain;
    const EntityId sceneId{std::string{"eof-scene"}};
    const EntityId mediaId{std::string{"eof-media"}};
    const std::u8string source = path.u8string();
    return Project{
        Library{{Media{mediaId, "EOF regression", std::string{
            reinterpret_cast<const char*>(source.data()), source.size()}}}},
        {Scene{sceneId, mediaId, {}, {}, Playback{false, 1.25F, 0.0F}, 12.0F}},
        {}};
}

bool testSmallTickNaturalCompletion(const std::filesystem::path& path)
{
    const auto project = makeProject(path);
    DesktopSceneEngine::Tests::AwaitAudioEngine engine;
    engine.initialize();
    engine.setSceneCompletionPolicy(
        DesktopSceneEngine::Engine::SceneCompletionPolicy::ExternalSceneCompletion);
    if (!require(engine.isInitialized(), "Engine should initialize")
        || !require(engine.bindProject(project, project.scenes.front().id),
            "Engine should bind the actual DIB/PCM AVI")
        || !require(engine.effectiveDuration() == 12.0F,
            "The actual AVI duration must be 12 seconds"))
    {
        return false;
    }

    constexpr float tick = 0.016F;
    bool completed = false;
    const auto initialAnchor = DesktopSceneEngine::Core::AudioOutputRecoveryEngineTestAccess::control(engine).clock;
    for (unsigned update = 1; update <= 700; ++update)
    {
        const float before = engine.playbackTime();
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
        engine.update(tick); // Deliberately not a clock advancement API.
        if (!DesktopSceneEngine::Tests::settle(engine)) return require(false, "tick effects reach durable terminal state");
        completed = engine.consumeNaturalSceneCompletion();
        const float after = engine.playbackTime();
        const double expected = DesktopSceneEngine::Audio::resolveAudioClockPosition(
            initialAnchor, DesktopSceneEngine::Audio::audioMonotonicNow());
        if (before >= 11.8F || completed)
            std::cout << "QPC EOF update=" << update << " before=" << before << " after=" << after
                << " expected=" << expected << " completed=" << completed << '\n';
        if (!require(after >= before && std::fabs(after - expected) < 0.04,
                "elapsed QPC clock advances through final-frame gap without clamp loss or rollback")
            || !require(!completed || after >= engine.effectiveDuration(),
                "decoder EOF does not complete before declared native duration")
            || !require(after >= engine.effectiveDuration() || !engine.isPaused(),
                "verified final-frame gap does not pause playback or enter failure hold")) return false;
        if (completed) break;
    }
    const float completedTime = engine.playbackTime();
    engine.update(tick);
    if (!DesktopSceneEngine::Tests::settle(engine)) return false;
    return require(completed && engine.isPaused(), "native elapsed clock reaches one natural completion")
        && require(!engine.consumeNaturalSceneCompletion() && engine.playbackTime() == completedTime,
            "natural completion remains paused, stable and emitted once");
}

bool testExplicitTailSeek(const std::filesystem::path& path)
{
    const auto project = makeProject(path);
    DesktopSceneEngine::Tests::AwaitAudioEngine engine;
    engine.initialize();
    if (!require(engine.bindProject(project, project.scenes.front().id),
            "Tail-seek project should bind")
        || !require(engine.seek(4.0F), "A valid explicit seek should succeed"))
    {
        return false;
    }
    const auto before = DesktopSceneEngine::Core::AudioOutputRecoveryEngineTestAccess::control(engine).clock;
    return require(!engine.seek(11.99F),
            "An explicit seek with no decoded target frame must still fail")
        && require(DesktopSceneEngine::Core::AudioOutputRecoveryEngineTestAccess::control(engine).clock.timingVersion == before.timingVersion
                && std::fabs(engine.playbackTime() - DesktopSceneEngine::Audio::resolveAudioClockPosition(
                    before, DesktopSceneEngine::Audio::audioMonotonicNow())) < 0.002,
            "failed explicit EOF seek preserves the committed anchor and natural elapsed progression")
        && require(!engine.consumeNaturalSceneCompletion(),
            "A failed explicit EOF seek must not create natural completion");
}

bool testEofDecodeFacts(const std::filesystem::path& path)
{
    using DesktopSceneEngine::Resources::VideoFrameDecodeOptions;
    using DesktopSceneEngine::Resources::VideoFrameDecodeStatus;
    DesktopSceneEngine::Resources::VideoResource video;
    if (!require(!video.lastFrameAtReachedEndOfStream(),
            "A new decoder must not report EOF")
        || !require(video.open(path) && video.duration() == 12.0F,
            "The real AVI should open with its declared duration"))
    {
        return false;
    }
    for (unsigned index = 0; index < 120; ++index)
    {
        if (!require(video.frameAt(static_cast<float>(index) / 10.0F)
                && !video.lastFrameAtReachedEndOfStream(),
                "Valid frame decoding must not report EOF"))
        {
            return false;
        }
    }
    const auto finalFrame = video.currentFrame();
    const std::string finalPixels{
        static_cast<const char*>(finalFrame.data), finalFrame.byteSize};
    if (!require(finalFrame.hasTimestamp
            && std::fabs(finalFrame.timestampSeconds - 11.9) < 0.000001,
            "The actual final video PTS must be 11.9 seconds")
        || !require(!video.frameAt(11.99F)
                && video.lastFrameAtReachedEndOfStream(),
            "Only a fully drained decoder should expose confirmed EOF")
        || !require(video.currentFrame().data == finalFrame.data
                && video.currentFrame().timestampSeconds == finalFrame.timestampSeconds
                && finalPixels == std::string{
                    static_cast<const char*>(video.currentFrame().data),
                    video.currentFrame().byteSize},
            "Confirmed EOF must preserve the real final frame and its pixels")
        || !require(video.frameAt(11.99F, VideoFrameDecodeOptions{})
                == VideoFrameDecodeStatus::Unavailable
                && video.lastFrameAtReachedEndOfStream(),
            "Preview decode status must remain Unavailable for EOF")
        || !require(!video.frameAt((std::numeric_limits<float>::quiet_NaN)())
                && !video.lastFrameAtReachedEndOfStream(),
            "Invalid numeric requests must clear the last-call EOF fact")
        || !require(!video.frameAt(11.99F)
                && video.lastFrameAtReachedEndOfStream(),
            "A valid repeated request may report the cached confirmed decoder EOF")
        || !require(!video.frameAt(-1.0F)
                && !video.lastFrameAtReachedEndOfStream(),
            "Negative requests must not masquerade as EOF")
        || !require(video.frameAt(4.0F)
                && !video.lastFrameAtReachedEndOfStream(),
            "A successful backward seek must reset confirmed EOF")
        || !require(!video.frameAt(11.99F)
                && video.lastFrameAtReachedEndOfStream(),
            "Decoding after a backward seek must be able to reach EOF again"))
    {
        return false;
    }

    std::atomic_bool cancel{true};
    VideoFrameDecodeOptions options;
    options.cancellationRequested = &cancel;
    if (!require(video.frameAt(11.99F, options) == VideoFrameDecodeStatus::Cancelled
                && !video.lastFrameAtReachedEndOfStream()
                && video.currentFrame().data == nullptr,
            "Cancellation must retain its existing status and clear EOF/frame state"))
    {
        return false;
    }
    cancel.store(false);
    if (!require(video.frameAt(0.0F, options) == VideoFrameDecodeStatus::Ready
                && !video.lastFrameAtReachedEndOfStream(),
            "A fresh decode after cancellation must reset EOF")
        || !require(!video.frameAt(11.99F)
                && video.lastFrameAtReachedEndOfStream(),
            "Fresh post-cancellation decoding must still reach EOF")
        || !require(!video.open({}) && !video.lastFrameAtReachedEndOfStream(),
            "A failed open must not leave a stale last-call EOF fact")
        || !require(video.open(path) && !video.lastFrameAtReachedEndOfStream()
                && video.currentFrame().data == nullptr
                && video.frameAt(0.0F) && !video.lastFrameAtReachedEndOfStream(),
            "Reopening must start with fresh decoder and EOF state"))
    {
        return false;
    }
    std::cout << "confirmed EOF / invalid request / backward seek / cancellation / reopen: PASS\n";
    return true;
}

bool testDecoderErrorRollback(const std::filesystem::path& path)
{
    DesktopSceneEngine::Resources::VideoResource video;
    if (!require(video.open(path) && video.frameAt(0.0F),
            "The error fixture must decode a valid initial frame")
        || !require(!video.frameAt(0.1F)
                && !video.lastFrameAtReachedEndOfStream(),
            "An invalid DIB packet must be a real decode failure, not EOF")
        || !require(!video.frameAt(0.2F)
                && !video.lastFrameAtReachedEndOfStream(),
            "A cached decoder failure must not become confirmed EOF"))
    {
        return false;
    }

    const auto project = makeProject(path);
    DesktopSceneEngine::Tests::AwaitAudioEngine engine;
    engine.initialize();
    engine.setSceneCompletionPolicy(
        DesktopSceneEngine::Engine::SceneCompletionPolicy::ExternalSceneCompletion);
    if (!require(engine.bindProject(project, project.scenes.front().id),
            "The error fixture must bind and upload its valid initial frame"))
    {
        return false;
    }
    using Access = DesktopSceneEngine::Core::AudioOutputRecoveryEngineTestAccess;
    const float confirmed = Access::confirmedPosition(engine);
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    engine.update();
    if (!DesktopSceneEngine::Tests::settle(engine)) return false;
    const auto heldPosition = engine.playbackTime();
    const auto failedTarget = Access::control(engine).failedTarget;
    std::cerr << "Failure hold confirmed=" << confirmed << " held=" << heldPosition
        << " failedTarget=" << failedTarget << " mode=" << static_cast<unsigned>(Access::control(engine).clock.mode)
        << " failureHeld=" << Access::control(engine).failureHeld
        << " lastConfirmed=" << Access::control(engine).lastConfirmedPosition << '\n';
    if (!require(Access::control(engine).failureHeld
            && Access::control(engine).clock.mode == DesktopSceneEngine::Audio::AudioClockMode::FailureHold
            && std::fabs(heldPosition - confirmed) < 0.002 && failedTarget > heldPosition,
        "real invalid packet rolls back to confirmed position and stores its failed future target")) return false;
    const auto embedded = DesktopSceneEngine::Tests::lane(engine, DesktopSceneEngine::Audio::AudioLane::Embedded);
    const auto cursor = embedded ? embedded->cursorFrames : 0;
    engine.device.grant(32);
    for (unsigned update = 0; update < 10; ++update)
    {
        engine.update(0.016F);
        if (!DesktopSceneEngine::Tests::settle(engine)) return false;
        const auto current = DesktopSceneEngine::Tests::lane(engine, DesktopSceneEngine::Audio::AudioLane::Embedded);
        if (!require(Access::control(engine).failureHeld && Access::control(engine).failedTarget == failedTarget
                && engine.playbackTime() == heldPosition && !engine.consumeNaturalSceneCompletion() && !engine.isPaused()
                && (!embedded || (current && current->source == embedded->source && current->cursorFrames == cursor)),
            "retrying failed target cannot fake recovery from cached held frame, advance embedded PCM, pause product state, or create EOF")) return false;
    }
    std::cout << "actual invalid-packet decoder error / native clock rollback: PASS\n";
    return true;
}

bool testDetachedPresentation(const std::filesystem::path& path)
{
    using namespace DesktopSceneEngine;

    const auto input = std::async(std::launch::async, [&] {
        const auto project = makeProject(path);
        Tests::AudioCaptureDevice device;
        Engine engine{Engine::PresentationMode::Detached};
        Core::AudioOutputRecoveryEngineTestAccess::installDevice(engine, device);
        if (!require(engine.initializeRuntimeOnly(), "detached runtime can start without graphics")
            || !require(Tests::effect(engine, [&] { return engine.bindProject(project, project.scenes.front().id); }),
                "a decoded video binds before any render owner exists"))
            return PresentationInput{};

        const auto initial = engine.presentationInput({4, 3});
        if (!require(initial.frame && !initial.frame->pixels.empty(), "first binding exports owning RGBA pixels"))
            return PresentationInput{};
        const auto savedPixels = initial.frame->pixels;
        if (!require(Tests::effect(engine, [&] { return engine.seek(4.0F); }),
                "detached seek commits before graphics")
            || !require(initial.frame->pixels == savedPixels,
                "codec reuse cannot overwrite a detached packet"))
            return PresentationInput{};
        const auto current = engine.presentationInput({4, 3});
        if (!require(current.frame && current.frame != initial.frame && current.canvasAspect == 4.0 / 3.0,
                "latest packet owns distinct pixels and the requested canvas"))
            return PresentationInput{};
        engine.shutdown();
        return current;
    }).get();

    EnginePresentation presentation;
    if (!require(input.frame && !input.frame->pixels.empty(), "the packet outlives its runtime and codec")
        || !require(presentation.present(input) == PresentationUploadResult::GraphicsUnavailable,
            "media readiness does not imply graphics readiness")
        || !require(presentation.initialize(HostedCompositionTarget{{320, 180}}).isValid(),
            "independent presentation initializes a real hosted target")
        || !require(presentation.present(input) == PresentationUploadResult::Updated,
            "the presentation owner realizes the retained media"))
        return false;

    auto invalid = input;
    auto malformed = std::make_shared<PresentationFrame>(*input.frame);
    malformed->pixels.clear();
    invalid.frame = std::move(malformed);
    return require(presentation.present(invalid) == PresentationUploadResult::ResourceFailure,
            "GPU input validation reports genuine upload failure")
        && require(presentation.present(input) == PresentationUploadResult::Updated,
            "failed upload leaves the previous packet usable");
}

bool testAsyncMediaPreparation(const std::filesystem::path& path, const std::filesystem::path& missingPath)
{
    using namespace DesktopSceneEngine;
    using Status = Engine::MediaPreparationStatus;
    using Resolution = Engine::MediaPresentationResolution;
    const auto project = makeProject(path);
    const auto wrongProject = makeProject(missingPath);
    const auto& scene = project.scenes.front().id;
    Tests::AudioCaptureDevice device;
    Engine engine{Engine::PresentationMode::Detached};
    Core::AudioOutputRecoveryEngineTestAccess::installDevice(engine, device);
    engine.setSceneCompletionPolicy(Engine::SceneCompletionPolicy::ExternalSceneCompletion);

    const auto wait = [&](Engine::MediaPreparationToken ticket, Status expected) {
        return Tests::waitUntil(engine, [&] { return engine.preparedMediaStatus(ticket) != Status::Pending; })
            && engine.preparedMediaStatus(ticket) == expected;
    };

    if (!require(engine.initializeRuntimeOnly() && engine.enableAsyncMediaPreparation(),
            "detached Engine enables the resource owner")
        || !require(!engine.bindProject(project, scene), "asynchronous mode rejects legacy synchronous bind"))
        return false;
    const auto first = engine.prepareSceneMedia(project, scene);
    if (!require(first && wait(*first, Status::Ready), "worker opens and decodes the ordinary AVI")
        || !require(engine.sceneCount() == 0 && !engine.presentationInput({4, 3}).frame,
            "preparation cannot create active runtime or expose pixels before commit")
        || !require(!engine.commitPreparedScene(wrongProject, scene, *first)
                && engine.preparedMediaStatus(*first) == Status::Ready,
            "ready ticket cannot be committed against a different source")
        || !require(Tests::effect(engine, [&] { return engine.commitPreparedScene(project, scene, *first, true); }),
            "commit adopts the prepared frame and exact paused binding"))
        return false;

    const auto initial = engine.presentationInput({4, 3}).frame;
    if (!require(initial && !initial->pixels.empty() && engine.isPaused() && engine.effectiveDuration() == 12.0F,
            "committed metadata and pixels originate from the real AVI")
        || !require(engine.pendingMediaPresentation() == first
                && engine.resolveMediaPresentation(*first, PresentationUploadResult::Updated) == Resolution::Acknowledged
                && !engine.pendingMediaPresentation(), "graphics ACK retires the exact initial frame checkpoint")
        || !require(!engine.seek(4.0F), "asynchronous mode rejects legacy synchronous seek"))
        return false;
    const auto failed = engine.prepareSceneMedia(wrongProject, scene);
    if (!require(failed && wait(*failed, Status::Failed), "missing replacement fails on resource owner")
        || !require(engine.isPaused() && engine.presentationInput({4, 3}).frame == initial,
            "failed preparation keeps the accepted paused binding"))
        return false;
    engine.cancelMediaPreparation(*failed);

    std::optional<Engine::MediaPreparationToken> newBinding;
    {
        auto replacement = makeProject(path);
        auto newScene = replacement.scenes.front();
        newScene.id = Domain::EntityId{"worker-owned-replacement"};
        replacement.scenes.push_back(newScene);
        newBinding = engine.prepareSceneMedia(replacement, newScene.id);
        if (!require(newBinding && wait(*newBinding, Status::Ready), "replacement codec is prepared while incumbent remains usable")
            || !require(Tests::effect(engine, [&] {
                    return engine.commitPreparedScene(replacement, newScene.id, *newBinding, true);
                }), "commit replaces the binding without transferring caller storage ownership"))
            return false;
    }
    if (!require(engine.sceneCount() == 2 && engine.activeSceneIndex() == 1,
            "detached render projection outlives the caller's prepared project storage")
        || !require(engine.resolveMediaPresentation(*first, PresentationUploadResult::ResourceFailure) == Resolution::Ignored
                && engine.resolveMediaPresentation(*newBinding, PresentationUploadResult::DeviceLost) == Resolution::Pending,
            "stale failure cannot supersede a newer binding and transient graphics keeps its checkpoint")
        || !require(Tests::effect(engine, [&] {
                return engine.resolveMediaPresentation(*newBinding, PresentationUploadResult::ResourceFailure)
                    == Resolution::RolledBack;
            }), "genuine failure restores the immediate prior accepted video binding")
        || !require(engine.sceneCount() == 1 && engine.activeSceneIndex() == 0 && engine.isPaused()
                && engine.playbackTime() == 0 && engine.presentationInput({4, 3}).frame == initial,
            "rollback restores ownership, paused transport, and the original immutable frame"))
        return false;

    const auto stale = engine.prepareSeek(1.0F);
    if (!require(stale.has_value(), "a seek can be admitted on the retained video"))
        return false;
    engine.cancelMediaPreparation(*stale);
    const auto tail = engine.prepareSeek(11.99F);
    if (!require(engine.preparedMediaStatus(*stale) == Status::Expired && tail && wait(*tail, Status::Failed),
            "cancelled tickets expire and explicit EOF seek remains a failure")
        || !require(engine.playbackTime() == 0 && !engine.naturalSceneCompletionPending()
                && engine.presentationInput({4, 3}).frame == initial,
            "seek failure does not modify transport, pixels or natural completion"))
        return false;
    engine.cancelMediaPreparation(*tail);

    const auto seek = engine.prepareSeek(4.0F);
    if (!require(seek && wait(*seek, Status::Ready), "video codec remains live after presentation rollback")
        || !require(Tests::effect(engine, [&] { return engine.commitPreparedSeek(*seek); }),
            "ready explicit seek gets a fresh presentation stamp")
        || !require(Tests::effect(engine, [&] {
                return engine.resolveMediaPresentation(*seek, PresentationUploadResult::ResourceFailure)
                    == Resolution::RolledBack;
            }) && engine.playbackTime() == 0 && engine.isPaused()
                && engine.presentationInput({4, 3}).frame == initial,
            "seek upload failure restores the previous position and same paused binding"))
        return false;

    const auto nearEnd = engine.prepareSeek(11.7F);
    if (!require(nearEnd && wait(*nearEnd, Status::Ready), "ordinary explicit seek succeeds after cancellation and EOF")
        || !require(Tests::effect(engine, [&] { return engine.commitPreparedSeek(*nearEnd); }),
            "commit applies the exact ready seek")
        || !require(std::fabs(engine.playbackTime() - 11.7F) < 0.001F && engine.isPaused()
                && engine.presentationInput({4, 3}).frame != initial,
            "seek position and pixels advance while transport stays paused")
        || !require(Tests::effect(engine, [&] { engine.togglePause(); return true; }), "resume the native clock")
        || !require(Tests::waitUntil(engine, [&] {
                engine.update();
                return engine.naturalSceneCompletionPending();
            }), "ordinary asynchronous video progresses through a natural completion"))
        return false;

    if (!require(engine.consumeNaturalSceneCompletion() && !engine.consumeNaturalSceneCompletion(),
            "the exact natural EOF fact is emitted once")
        || !require(engine.playbackTime() == engine.effectiveDuration(),
            "natural EOF preserves declared media duration"))
        return false;
    engine.beginMediaShutdown();
    engine.beginAudioShutdown();
    if (!require(Tests::waitUntil(engine, [&] { return engine.mediaShutdownComplete() && engine.audioShutdownComplete(); }),
            "all resource ownership and the audio lane retire before Close"))
        return false;
    engine.shutdown();
    std::cout << "CP3 real asynchronous media preparation / stamped rollback / seek / natural EOF / retirement: PASS\n";
    return true;
}

bool testEarlierReceiptCannotRetireNewIncumbent(const std::filesystem::path& path)
{
    using Producer = DesktopSceneEngine::Resources::DetachedMediaProducer;
    constexpr Producer::Token incumbent = 1, first = 2, second = 3;
    std::promise<void> ready, resume;
    auto secondReady = ready.get_future();
    auto releaseWorker = resume.get_future();
    Producer producer{nullptr, [&](Producer::Token published) {
        if (published != second) return;
        ready.set_value();
        releaseWorker.wait();
    }};
    struct ReleaseOnExit
    {
        std::promise<void>& promise;
        bool released = false;
        void release()
        {
            if (std::exchange(released, true)) return;
            promise.set_value();
        }
        ~ReleaseOnExit() { release(); }
    } gate{resume};

    const auto wait = [](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
        do
        {
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    };
    const auto prepare = [&](Producer::Token token) {
        return producer.prepareScene(token, path, 12, 0)
            && wait([&] { return producer.preparedState(token) != Producer::State::Pending; })
            && producer.preparedState(token) == Producer::State::Ready;
    };
    const auto decode = [&](Producer::Token token) {
        std::optional<Producer::Frame> frame;
        return producer.requestFrame(token, 1.0F)
            && wait([&] { frame = producer.takeFrame(); return frame.has_value(); })
            && frame->binding == token && frame->state == Producer::FrameState::Ready && frame->pixels;
    };
    if (!require(prepare(incumbent) && producer.commit(incumbent, true) && decode(incumbent),
            "prime a real incumbent decoder")
        || !require(prepare(first) && producer.commit(first, true) && decode(first),
            "promote the first accepted decoder while retaining the incumbent")
        || !require(prepare(second) && secondReady.wait_for(std::chrono::seconds{3}) == std::future_status::ready,
            "park the real worker after publishing the newer candidate without dispatching it"))
        return false;

    // Model the control-side Updated receipt for the first binding followed
    // by a logical commit of the newer one. Both requests are queued before P
    // can dispatch, with promotion deliberately winning its normal priority.
    producer.releasePreviousBinding(first);
    if (!require(producer.commit(second, true).has_value(), "queue the newer promotion before releasing P"))
        return false;
    gate.release();
    if (!require(decode(second), "the newer decoder is installed before its presentation result")) return false;

    // A genuine ResourceFailure for the second binding restores the first.
    // Reading real pixels proves that only the prior retired incumbent was released.
    producer.restorePreviousBinding(first);
    if (!require(decode(first), "earlier ACK must not retire the newer binding's real rollback decoder"))
        return false;
    std::cout << "CP3 earlier presentation ACK / newer promotion / real rollback decoder: PASS\n";
    return true;
}

}

int main(int argc, char** argv)
{
    TestMedia media;
    if (!require(std::filesystem::create_directory(media.directory),
            "The owned fixture directory should be created"))
    {
        return 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--decode-environment-only")
        return testDecodeLayoutIgnoresDiagnosticEnvironment(media.decodeLayoutPath) ? 0 : 1;
    if (!require(writeTestAvi(media.path), "The actual AVI should be written")
        || !require(writeTestAvi(media.invalidPath, true),
            "The deliberately invalid-packet AVI should be written"))
    {
        return 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--cp3-media-only")
        return testAsyncMediaPreparation(media.path, media.directory / "missing.avi") ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--cp3-media-ack-order-only")
        return testEarlierReceiptCannotRetireNewIncumbent(media.path) ? 0 : 1;

    return testSmallTickNaturalCompletion(media.path)
            && testExplicitTailSeek(media.path)
            && testEofDecodeFacts(media.path)
            && testDecoderErrorRollback(media.invalidPath)
            && testDetachedPresentation(media.path)
            && testAsyncMediaPreparation(media.path, media.directory / "missing.avi")
            && testEarlierReceiptCannotRetireNewIncumbent(media.path)
            && testDecodeLayoutIgnoresDiagnosticEnvironment(media.decodeLayoutPath)
        ? 0 : 1;
}
