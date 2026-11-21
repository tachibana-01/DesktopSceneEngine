#include "GraphicsBackend.h"
#include "Renderer.h"
#include <DesktopSceneEngine/Engine.h>
#include <DesktopSceneEngine/HostedCompositionContent.h>
#include <DesktopSceneEngine/Domain/Library.h>
#include <DesktopSceneEngine/Domain/Media.h>
#include <DesktopSceneEngine/Domain/Playback.h>
#include <DesktopSceneEngine/Domain/Project.h>
#include <DesktopSceneEngine/Domain/Scene.h>
#include <Windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

namespace DesktopSceneEngine::Rendering
{
// Reuse the existing Engine/Renderer diagnostic friendship. Production frames
// still enter through Engine::render; no substituted renderer or copied TU.
class CanvasTargetFitTestAccess
{
public:
    static GraphicsBackend& backend(Engine& engine) { return engine.renderer().backend_; }
};
}

namespace
{
using namespace DesktopSceneEngine;
using Rendering::GraphicsBackend;
using Rendering::Detail::FramePreparationResult;
using Microsoft::WRL::ComPtr;

static_assert(!std::is_copy_constructible_v<CompositionContentLease>);
static_assert(!std::is_copy_assignable_v<CompositionContentLease>);
static_assert(std::is_nothrow_move_constructible_v<CompositionContentLease>);
static_assert(std::is_nothrow_move_assignable_v<CompositionContentLease>);
static_assert(Engine::ExternalWindowTarget{}.transport == PresentationTransport::HwndSwapChain);

bool require(bool condition, const char* message)
{
    if (!condition) std::cerr << "Test failed: " << message << '\n';
    return condition;
}

bool extentIs(const CompositionContentLease& lease, int width, int height)
{
    return lease.extent().width == width && lease.extent().height == height;
}

bool attachableWithExtent(const CompositionContentLease& lease, int width, int height)
{
    ComPtr<IDXGISwapChain1> swapChain;
    DXGI_SWAP_CHAIN_DESC1 description{};
    return require(lease.content()
            && SUCCEEDED(lease.content()->QueryInterface(IID_PPV_ARGS(&swapChain)))
            && SUCCEEDED(swapChain->GetDesc1(&description)),
            "lease owns real Host-attachable DXGI composition content")
        && require(description.Width == static_cast<UINT>(width)
                && description.Height == static_cast<UINT>(height)
                && description.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
                && description.AlphaMode == DXGI_ALPHA_MODE_PREMULTIPLIED,
            "leased content's native allocation matches the expected dimensions");
}

bool attachAndReleaseTestConsumer(const CompositionContentLease& lease)
{
    // This consumer belongs only to the test. No HWND, target, root or Session
    // tree is constructed; Engine continues to own only the content resource.
    ComPtr<IDCompositionDevice> consumer;
    ComPtr<IDCompositionVisual> visual;
    if (!require(lease.eligible()
            && SUCCEEDED(DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&consumer)))
            && SUCCEEDED(consumer->CreateVisual(&visual))
            && SUCCEEDED(visual->SetContent(lease.content()))
            && SUCCEEDED(consumer->Commit())
            && SUCCEEDED(consumer->WaitForCommitCompletion()),
            "independent native consumer accepts and commits the leased content")) return false;
    return require(SUCCEEDED(visual->SetContent(nullptr))
            && SUCCEEDED(consumer->Commit())
            && SUCCEEDED(consumer->WaitForCommitCompletion()),
        "independent native consumer safely releases leased content");
}

bool noEngineCompositionTree(GraphicsBackend& backend, PresentationTargetId id)
{
    const auto target = backend.presentationTargetDiagnosticsForTesting(id);
    const auto shared = backend.compositionDiagnosticsForTesting();
    return require(target.deviceIdentity != 0 && target.swapChainIdentity != 0
            && !target.hasCompositionTarget && !target.hasCompositionVisual
            && !target.compositionTreeSubmitted && target.compositionTargetIdentity == 0
            && target.compositionVisualIdentity == 0 && shared.capabilityIdentity == 0
            && shared.capabilityCreationCount == 0 && shared.commitCount == 0
            && shared.waitCount == 0,
        "hosted Engine creates content without a DComp capability, target, visual or Commit");
}

bool present(GraphicsBackend& backend)
{
    if (!require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "windowless hosted allocation is drawable")) return false;
    backend.renderPrepared({});
    return true;
}

bool testEmptyAndCreation()
{
    CompositionContentLease empty;
    if (!require(!empty.content() && !empty.eligible() && !empty.isCurrent()
            && !empty.targetId().isValid() && empty.deviceGeneration() == 0
            && empty.contentGeneration() == 0 && extentIs(empty, 0, 0),
            "default lease is an empty value")) return false;
    GraphicsBackend invalid;
    if (!require(!invalid.initialize(HostedCompositionTarget{{-1, 180}}).isValid(),
            "invalid hosted allocation is rejected without creating a target")) return false;
    GraphicsBackend backend;
    const auto id = backend.initialize(HostedCompositionTarget{{320, 180}});
    if (!require(id.isValid() && backend.presentationTargetCountForTesting() == 1,
            "hosted target initializes without any HWND or hidden second target")
        || !noEngineCompositionTree(backend, id)
        || !require(!backend.acquireHostedCompositionContent(id).eligible(),
            "creation alone cannot publish eligible unpresented content")
        || !present(backend)) return false;
    auto lease = backend.acquireHostedCompositionContent(id);
    if (!require(lease.eligible() && lease.isCurrent() && lease.targetId() == id
            && lease.deviceGeneration() == backend.presentationTargetInfo(id).deviceGeneration
            && lease.deviceGeneration() != 0 && lease.contentGeneration() != 0
            && extentIs(lease, 320, 180),
            "successful real Present publishes truthful allocation, target and generations")
        || !attachableWithExtent(lease, 320, 180)
        || !attachAndReleaseTestConsumer(lease)
        || !noEngineCompositionTree(backend, id)) return false;
    auto moved = std::move(lease);
    return require(!lease.content() && !lease.eligible() && !lease.isCurrent()
            && moved.content() && moved.eligible() && moved.isCurrent(),
        "move transfers the strong content lease and empties the source");
}

bool testResizeSnapshots()
{
    GraphicsBackend backend;
    const auto id = backend.initialize(HostedCompositionTarget{{320, 180}});
    if (!require(id.isValid(), "resize hosted target initializes") || !present(backend)) return false;
    auto old = backend.acquireHostedCompositionContent(id);
    const auto oldContent = old.content();
    const auto oldGeneration = old.contentGeneration();
    const auto oldDevice = old.deviceGeneration();
    if (!require(backend.setHostedPresentationExtent(id, {480, 270}), "explicit extent accepted")
        || !require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "hosted extent reconciles without a window")) return false;
    if (!require(!old.eligible() && !old.isCurrent() && extentIs(old, 320, 180),
            "same-object resize retires old immutable extent snapshot before publication")
        || !require(!backend.acquireHostedCompositionContent(id).eligible(),
            "resized buffers are ineligible before successful Present")) return false;
    backend.renderPrepared({});
    auto resized = backend.acquireHostedCompositionContent(id);
    return require(resized.eligible() && resized.isCurrent() && extentIs(resized, 480, 270)
            && resized.content() == oldContent && resized.contentGeneration() == oldGeneration
            && resized.deviceGeneration() == oldDevice,
            "ResizeBuffers preserves attachable identity and content generation")
        && attachableWithExtent(resized, 480, 270)
        && require(!backend.setHostedPresentationExtent(id, {-1, 270}),
            "invalid extent update is rejected")
        && require(!backend.setHostedPresentationExtent({}, {640, 360}),
            "unknown target cannot receive hosted allocation updates")
        && noEngineCompositionTree(backend, id);
}

bool testFailedResize(HRESULT result)
{
    GraphicsBackend backend;
    const auto id = backend.initialize(HostedCompositionTarget{{320, 180}});
    if (!require(id.isValid(), "failed resize target initializes") || !present(backend)) return false;
    auto old = backend.acquireHostedCompositionContent(id);
    if (!require(backend.forceNextHostedResizeResultForTesting(id, static_cast<std::int32_t>(result))
            && backend.setHostedPresentationExtent(id, {640, 360}),
            "inject actual hosted resize failure path")) return false;
    (void)backend.beginFrame();
    const auto unavailable = backend.acquireHostedCompositionContent(id);
    return require(!old.eligible() && !old.isCurrent() && !unavailable.eligible(),
            "failed or busy ResizeBuffers cannot reuse stale lease authority")
        && require(!unavailable.content() || !extentIs(unavailable, 640, 360),
            "failed ResizeBuffers never claims the requested allocation")
        && attachableWithExtent(old, 320, 180);
}

bool testFreshPresentFailure(HRESULT result)
{
    GraphicsBackend backend;
    const auto id = backend.initialize(HostedCompositionTarget{{320, 180}});
    if (!require(id.isValid() && backend.forceNextDxgiPresentResultForTesting(
            id, static_cast<std::int32_t>(result)), "arm unready Present result")
        || !present(backend)) return false;
    return require(!backend.acquireHostedCompositionContent(id).eligible(),
        "busy, occluded or failed first Present cannot publish fresh content");
}

bool testPostResizePresentFailure(HRESULT result)
{
    GraphicsBackend backend;
    const auto id = backend.initialize(HostedCompositionTarget{{320, 180}});
    if (!require(id.isValid(), "post-resize Present failure target initializes")
        || !present(backend)) return false;
    auto old = backend.acquireHostedCompositionContent(id);
    if (!require(old.eligible() && backend.setHostedPresentationExtent(id, {480, 270})
            && backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "real native ResizeBuffers succeeds before the failed Present")
        || !require(backend.presentationTargetDiagnosticsForTesting(id).successfulResizeCount == 1,
            "post-resize Present fixture observes one actual successful allocation")
        || !attachableWithExtent(old, 480, 270)
        || !require(!old.eligible() && !old.isCurrent() && extentIs(old, 320, 180)
            && !backend.acquireHostedCompositionContent(id).eligible(),
            "successful allocation retires old immutable snapshot before Present")
        || !require(backend.forceNextDxgiPresentResultForTesting(id,
            static_cast<std::int32_t>(result)), "arm first Present after successful resize")) return false;
    backend.renderPrepared({});
    const auto unavailable = backend.acquireHostedCompositionContent(id);
    return require(!old.eligible() && !old.isCurrent() && !unavailable.eligible(),
            "busy or failed post-resize Present cannot republish stale allocation authority")
        && require(!unavailable.content() || extentIs(unavailable, 480, 270),
            "post-resize acquisition cannot report the old extent for the new native buffers")
        && noEngineCompositionTree(backend, id);
}

bool testExternalWindowCannotSelectHosted()
{
    // A non-null sentinel reaches transport validation without constructing a
    // window. Hosted transport must reject it before inspecting a native HWND.
    const Engine::ExternalWindowTarget misuse{
        reinterpret_cast<void*>(std::uintptr_t{1}), PresentationTransport::HostedCompositionContent};
    Engine invalid;
    if (!require(!invalid.initialize(misuse).isValid() && !invalid.isInitialized(),
            "legacy external-window initialize cannot silently select hosted mode")) return false;
    Engine hosted;
    const auto id = hosted.initialize(HostedCompositionTarget{{128, 72}});
    return require(id.isValid() && !hosted.attachPresentationTarget(misuse).isValid(),
        "legacy external-window attachment rejects hosted enum misuse");
}

bool testNativeResizeFailureAndRetry()
{
    GraphicsBackend backend;
    const auto id = backend.initialize(HostedCompositionTarget{{320, 180}});
    if (!require(id.isValid(), "native resize failure target initializes") || !present(backend)) return false;
    auto old = backend.acquireHostedCompositionContent(id);
    ComPtr<IDXGISwapChain1> chain;
    ComPtr<ID3D11Texture2D> heldBackBuffer;
    if (!require(old.content() && SUCCEEDED(old.content()->QueryInterface(IID_PPV_ARGS(&chain)))
            && SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&heldBackBuffer))),
            "hold a real native backbuffer reference to prevent ResizeBuffers")
        || !require(backend.setHostedPresentationExtent(id, {640, 360}),
            "request extent while native backbuffer is externally held")) return false;
    (void)backend.beginFrame();
    const auto rejected = backend.acquireHostedCompositionContent(id);
    if (!require(!old.eligible() && !old.isCurrent() && !rejected.eligible(),
            "real DXGI ResizeBuffers failure revokes stale snapshot authority")
        || !require(backend.presentationTargetDiagnosticsForTesting(id).successfulResizeCount == 0,
            "native failed ResizeBuffers is not counted as a successful allocation")
        || !attachableWithExtent(old, 320, 180)) return false;
    heldBackBuffer.Reset();
    if (!require(backend.setHostedPresentationExtent(id, {480, 270}),
            "new explicit extent authorizes retry after native backbuffer release")
        || !require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "existing allocation reconcile recovers after native reference is released")
        || !require(!backend.acquireHostedCompositionContent(id).eligible(),
            "successful retry still requires first Present")) return false;
    backend.renderPrepared({});
    auto fresh = backend.acquireHostedCompositionContent(id);
    return require(fresh.eligible() && fresh.isCurrent() && extentIs(fresh, 480, 270)
            && fresh.content() == old.content() && fresh.contentGeneration() == old.contentGeneration(),
            "native retry publishes actual allocation without inventing content identity")
        && attachableWithExtent(fresh, 480, 270);
}

bool testReplacementAndRetirement()
{
    GraphicsBackend backend;
    const auto id = backend.initialize(HostedCompositionTarget{{320, 180}});
    const auto peer = backend.attachPresentationTarget(HostedCompositionTarget{{160, 90}});
    if (!require(id.isValid() && peer.isValid() && id != peer, "distinct hosted targets attach")
        || !present(backend)) return false;
    auto old = backend.acquireHostedCompositionContent(id);
    auto peerLease = backend.acquireHostedCompositionContent(peer);
    if (!require(backend.requestHostedContentReplacementForTesting(id),
            "request real same-device swap-chain identity replacement")
        || !require(backend.beginFrame() == FramePreparationResult::ReadyTargetsAvailable,
            "replacement allocation is drawable")
        || !require(!backend.acquireHostedCompositionContent(id).eligible(),
            "fresh replacement remains unpublished before successful Present")) return false;
    backend.renderPrepared({});
    auto fresh = backend.acquireHostedCompositionContent(id);
    if (!require(fresh.eligible() && fresh.isCurrent() && fresh.content() != old.content()
            && fresh.contentGeneration() > old.contentGeneration()
            && fresh.deviceGeneration() == old.deviceGeneration(),
            "same-device replacement advances content identity only")
        || !require(old.content() && old.eligible() && !old.isCurrent(),
            "held valid old content survives publication of its replacement")
        || !attachableWithExtent(old, 320, 180)
        || !require(backend.detachPresentationTarget(id), "hosted target detaches")
        || !require(!old.eligible() && !old.isCurrent() && !fresh.eligible() && !fresh.isCurrent()
            && !backend.acquireHostedCompositionContent(id).content(),
            "target retirement revokes all generations and rejects new acquisition")
        || !require(peerLease.eligible() && peerLease.isCurrent(),
            "target retirement leaves independent hosted peer valid")) return false;
    backend.shutdown();
    return require(!peerLease.eligible() && !peerLease.isCurrent(),
            "backend shutdown invalidates held peer lease")
        && attachableWithExtent(old, 320, 180)
        && attachableWithExtent(fresh, 320, 180)
        && attachableWithExtent(peerLease, 160, 90);
}

bool testDeviceInvalidation()
{
    GraphicsBackend backend;
    const auto id = backend.initialize(HostedCompositionTarget{{320, 180}});
    if (!require(id.isValid(), "device invalidation target initializes") || !present(backend)) return false;
    auto old = backend.acquireHostedCompositionContent(id);
    const auto generation = old.deviceGeneration();
    if (!require(backend.forceNextDxgiPresentResultForTesting(id,
            static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED),
            static_cast<std::int32_t>(DXGI_ERROR_DEVICE_REMOVED)), "arm device removal")
        || !present(backend)
        || !require(!old.eligible() && !old.isCurrent()
            && !backend.acquireHostedCompositionContent(id).eligible(),
            "device retirement invalidates held semantic authority")) return false;
    if (!require(backend.recoverGraphicsGeneration(nullptr)
            == Rendering::Detail::GenerationReplacementResult::GenerationAvailable,
            "existing generation recovery realizes hosted target")
        || !require(!backend.acquireHostedCompositionContent(id).eligible(),
            "recovered fresh generation requires real Present") || !present(backend)) return false;
    auto recovered = backend.acquireHostedCompositionContent(id);
    return require(recovered.eligible() && recovered.isCurrent()
            && recovered.deviceGeneration() != generation,
            "lease reports existing real recovered device generation")
        && noEngineCompositionTree(backend, id);
}

bool testEngineDestructorRetiresLease()
{
    CompositionContentLease held;
    {
        Engine engine;
        const auto id = engine.initialize(HostedCompositionTarget{{128, 72}});
        if (!require(id.isValid(), "destructor-only Engine target initializes")) return false;
        engine.render();
        held = engine.acquireHostedCompositionContent(id);
        if (!require(held.eligible() && held.isCurrent(),
                "destructor-only fixture holds a presented lease")) return false;
    }
    return require(held.content() && !held.eligible() && !held.isCurrent(),
            "Engine destructor alone retires lease without requiring explicit shutdown")
        && attachableWithExtent(held, 128, 72);
}

// An owned DIB AVI exercises the production FFmpeg demux/decode path, matching
// EnginePlaybackEofTest's existing fixture format without external media/tools.
std::string word(std::uint32_t value)
{
    std::string data;
    for (unsigned shift = 0; shift != 32; shift += 8)
        data.push_back(static_cast<char>((value >> shift) & 255));
    return data;
}

std::string chunk(const char* tag, const std::string& content)
{
    return std::string(tag, 4) + word(static_cast<std::uint32_t>(content.size()))
        + content + (content.size() % 2 ? std::string(1, '\0') : std::string{});
}

struct OwnedVideo
{
    std::filesystem::path directory = std::filesystem::temp_directory_path()
        / ("DesktopSceneEngine-Hosted-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = directory / "owned-video.avi";
    bool create()
    {
        constexpr unsigned width = 64, height = 36, frames = 20, fps = 10;
        constexpr unsigned frameBytes = width * height * 3;
        std::string main, stream{"vidsDIB ", 8}, format;
        for (const auto value : {100000U, frameBytes * fps, 0U, 0x10U,
                frames, 0U, 1U, frameBytes, width, height, 0U, 0U, 0U, 0U}) main += word(value);
        for (const auto value : {0U, 0U, 0U, 1U, fps, 0U, frames,
                frameBytes, 0xffffffffU, 0U}) stream += word(value);
        stream += std::string(4, '\0');
        stream.push_back(static_cast<char>(width)); stream.push_back('\0');
        stream.push_back(static_cast<char>(height)); stream.push_back('\0');
        for (const auto value : {40U, width, height, 0x00180001U, 0U,
                frameBytes, 0U, 0U, 0U, 0U}) format += word(value);
        const auto header = chunk("LIST", std::string("hdrl", 4) + chunk("avih", main)
            + chunk("LIST", std::string("strl", 4) + chunk("strh", stream) + chunk("strf", format)));
        std::string movie{"movi", 4}, index;
        for (unsigned frame = 0; frame != frames; ++frame)
        {
            std::string pixels(frameBytes, '\0');
            for (unsigned offset = 0; offset != frameBytes; offset += 3)
            {
                pixels[offset] = static_cast<char>(40 + frame);
                pixels[offset + 1] = static_cast<char>(80 + frame);
                pixels[offset + 2] = static_cast<char>(220 - frame);
            }
            index += std::string("00db", 4) + word(0x10)
                + word(static_cast<std::uint32_t>(movie.size())) + word(frameBytes);
            movie += chunk("00db", pixels);
        }
        const auto data = chunk("RIFF", std::string("AVI ", 4) + header
            + chunk("LIST", movie) + chunk("idx1", index));
        std::error_code error;
        std::filesystem::create_directory(directory, error);
        std::ofstream output(path, std::ios::binary);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        return output.good();
    }
    ~OwnedVideo()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(directory, ignored);
    }
};

bool testRealEngineMediaAndShutdown()
{
    OwnedVideo video;
    if (!require(video.create(), "create owned real encoded-container media")) return false;
    using namespace Domain;
    const EntityId sceneId{std::string("hosted-scene")}, mediaId{std::string("hosted-media")};
    const auto utf8 = video.path.u8string();
    const Project project{Library{{Media{mediaId, "Hosted regression",
        std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size())}}},
        {Scene{sceneId, mediaId, {}, {}, Playback{false, 1.0F, 0.0F}, 2.0F}}, {}};
    CompositionContentLease held;
    {
        Engine engine;
        const auto id = engine.initialize(HostedCompositionTarget{{320, 180}});
        if (!require(id.isValid() && engine.isInitialized(), "production hosted Engine initializes")
            || !require(!engine.acquireHostedCompositionContent(id).eligible(),
                "Engine initialize does not publish unpresented content")
            || !require(engine.bindProject(project, sceneId), "production Engine binds and decodes owned AVI")) return false;
        auto& backend = Rendering::CanvasTargetFitTestAccess::backend(engine);
        if (!require(backend.captureNextPresentationPixelForTesting(id), "capture real renderer output")) return false;
        engine.render();
        held = engine.acquireHostedCompositionContent(id);
        const auto diagnostics = backend.presentationTargetDiagnosticsForTesting(id);
        if (!require(held.eligible() && held.isCurrent() && diagnostics.drawCount == 1
                && diagnostics.presentAttemptCount == 1 && diagnostics.capturedPixelValid
                && diagnostics.capturedPixelRgba == 0xff2850dcU
                && backend.mediaUploadCountForTesting() == 1
                && backend.presentationTargetCountForTesting() == 1,
                "production Engine decodes, uploads, draws and Presents actual media pixels")
            || !noEngineCompositionTree(backend, id)
            || !attachableWithExtent(held, 320, 180)) return false;
        const auto peer = engine.attachPresentationTarget(HostedCompositionTarget{{160, 90}});
        if (!require(peer.isValid() && engine.setHostedPresentationExtent(peer, {192, 108}),
                "public Engine forwards hosted attachment and explicit extent")) return false;
        engine.render();
        auto peerLease = engine.acquireHostedCompositionContent(peer);
        if (!require(peerLease.eligible() && extentIs(peerLease, 192, 108)
                && engine.detachPresentationTarget(peer)
                && !peerLease.eligible() && !engine.acquireHostedCompositionContent(peer).content(),
                "public Engine detach retires hosted lease")) return false;
        engine.shutdown();
        if (!require(!held.eligible() && !held.isCurrent()
                && !engine.acquireHostedCompositionContent(id).content(),
                "Engine shutdown invalidates held lease while object remains alive")) return false;
    }
    return require(!held.eligible() && !held.isCurrent(),
            "held lease safely outlives destroyed Engine without lifecycle callbacks")
        && attachableWithExtent(held, 320, 180);
}
}

int main()
{
    bool success = true;
    const auto run = [&](const char* name, auto test)
    {
        const bool passed = test();
        std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
        success = passed && success;
    };
    run("empty, windowless creation, native allocation and move", testEmptyAndCreation);
    run("same-content resize snapshot and generations", testResizeSnapshots);
    run("failed ResizeBuffers", [] { return testFailedResize(E_FAIL); });
    run("busy ResizeBuffers", [] { return testFailedResize(DXGI_ERROR_WAS_STILL_DRAWING); });
    run("real held-backbuffer ResizeBuffers failure and retry", testNativeResizeFailureAndRetry);
    run("fresh busy Present", [] { return testFreshPresentFailure(DXGI_ERROR_WAS_STILL_DRAWING); });
    run("fresh occluded Present", [] { return testFreshPresentFailure(DXGI_STATUS_OCCLUDED); });
    run("fresh failed Present", [] { return testFreshPresentFailure(E_FAIL); });
    run("successful ResizeBuffers then busy Present", [] { return testPostResizePresentFailure(DXGI_ERROR_WAS_STILL_DRAWING); });
    run("successful ResizeBuffers then failed Present", [] { return testPostResizePresentFailure(E_FAIL); });
    run("legacy external-window API cannot select hosted mode", testExternalWindowCannotSelectHosted);
    run("replacement, detach, held COM lifetime and shutdown", testReplacementAndRetirement);
    run("device retirement and existing recovery", testDeviceInvalidation);
    run("Engine destructor retires held lease", testEngineDestructorRetiresLease);
    run("production Engine real media and held-lease destruction", testRealEngineMediaAndShutdown);
    return success ? 0 : 1;
}
