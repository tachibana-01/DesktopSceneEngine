#include "GraphicsBackend.h"
#include "Renderer.h"
#include <DesktopSceneEngine/Engine.h>
#include <Windows.h>
#include <dxgi.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace DesktopSceneEngine::Rendering
{
// Only fixture upload, GPU readback and diagnostics cross this test seam.
// The frame under test enters through Engine's existing public render API.
class CanvasTargetFitTestAccess
{
public:
    static GraphicsBackend& backend(Engine& engine) { return engine.renderer_->backend_; }
};
}

namespace
{
using DesktopSceneEngine::Engine;
using DesktopSceneEngine::Domain::AspectRatio;
using DesktopSceneEngine::PresentationTargetId;
using DesktopSceneEngine::PresentationTransport;
using DesktopSceneEngine::CanvasOutsideInstruction;
using DesktopSceneEngine::CanvasOutsideMode;
using DesktopSceneEngine::Rendering::CanvasTargetFitTestAccess;
using DesktopSceneEngine::Rendering::GraphicsBackend;
constexpr std::array<unsigned char, 4> Content{220, 80, 40, 128};
constexpr std::array<unsigned char, 4> OpaqueContent{220, 80, 40, 255};
constexpr std::array<unsigned char, 4> Gutter{26, 26, 26, 255};

bool require(bool condition, const char* message)
{
    if (!condition) std::cerr << "Test failed: " << message << '\n';
    return condition;
}

class TestWindow
{
public:
    TestWindow(int width, int height, int x)
    {
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"DSECanvasTargetFitTest";
        RegisterClassW(&type);
        handle = CreateWindowExW(WS_EX_NOACTIVATE, type.lpszClassName,
            L"L6 Canvas Fit test-owned window", WS_POPUP, x, 100, width, height,
            nullptr, nullptr, type.hInstance, nullptr);
        ShowWindow(handle, SW_SHOWNOACTIVATE);
    }
    ~TestWindow() { if (handle) DestroyWindow(handle); }
    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;
    bool resize(int width, int height)
    {
        return SetWindowPos(handle, nullptr, 0, 0, width, height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
    }
    HWND handle = nullptr;
};

bool pixelMatches(GraphicsBackend& backend, PresentationTargetId id,
    const std::array<unsigned char, 4>& expected, const char* label)
{
    const auto d = backend.presentationTargetDiagnosticsForTesting(id);
    const auto pixel = d.capturedPixelRgba;
    // D3D clear-to-UNORM conversion may differ by one level at 0.1 * 255.
    const int tolerance = expected == Gutter ? 1 : 0;
    std::cout << label << " readback=(" << (pixel & 255U) << ',' << ((pixel >> 8) & 255U)
        << ',' << ((pixel >> 16) & 255U) << ',' << ((pixel >> 24) & 255U) << ")\n";
    return require(d.capturedPixelValid && std::abs(int(pixel & 255U) - expected[0]) <= tolerance
            && std::abs(int((pixel >> 8) & 255U) - expected[1]) <= tolerance
            && std::abs(int((pixel >> 16) & 255U) - expected[2]) <= tolerance
            && ((pixel >> 24) & 255U) == expected[3], label);
}

bool verifyEngineOutput()
{
    TestWindow ordinary{320, 320, 100}, composition{320, 320, 500};
    Engine engine;
    const auto a = engine.initialize({ordinary.handle});
    const auto b = engine.attachPresentationTarget({composition.handle,
        PresentationTransport::DirectCompositionSwapChain});
    if (!require(a.isValid() && b.isValid(), "real ordinary/composition HWND preconditions")) return false;
    if (!require(engine.setPresentationPacingTarget(b), "composition pacing selection")) return false;
    auto& backend = CanvasTargetFitTestAccess::backend(engine);
    if (!require(backend.updateImage(Content.data(), 1, 1, 4, 4, Content.size())
            == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated,
        "upload one shared fixture")) return false;
    const auto capability = backend.compositionDiagnosticsForTesting();
    const auto generation = backend.presentationTargetInfo(a).deviceGeneration;
    std::uint64_t frames = 0;
    std::uint64_t draws = 0;
    const auto sample = [&](int ax, int ay, int bx, int by,
        const auto& expectedA, const auto& expectedB, std::optional<AspectRatio> canvas,
        const char* label)
    {
        if (!require(backend.beginFrame() == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable
                && backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(S_OK))
                && backend.captureNextPresentationPixelForTesting(a, ax, ay)
                && backend.captureNextPresentationPixelForTesting(b, bx, by), "prepare paired GPU samples")) return false;
        if (canvas) engine.render(*canvas); else engine.render();
        ++frames;
        if (!canvas || canvas->isValid()) ++draws;
        bool ok = pixelMatches(backend, a, expectedA, label);
        ok = pixelMatches(backend, b, expectedB, label) && ok;
        const auto ad = backend.presentationTargetDiagnosticsForTesting(a);
        const auto bd = backend.presentationTargetDiagnosticsForTesting(b);
        return require(ad.drawCount == draws && bd.drawCount == draws
                && ad.presentAttemptCount == frames && bd.presentAttemptCount == frames
                && backend.mediaUploadCountForTesting() == 1,
                "one frame submits both targets without duplicate upload")
            && require(ad.lastPresentSyncInterval == 0 && ad.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT
                && bd.lastPresentSyncInterval == 1 && bd.lastPresentFlags == 0
                && ad.lastPresentOrder < bd.lastPresentOrder,
                "Canvas Fit preserves non-pacer first and one VSync authority") && ok;
    };
    // First fill both RTVs, then Fit: outside pixels must not retain stale content.
    bool ok = sample(160, 5, 160, 5, OpaqueContent, OpaqueContent, std::nullopt, "legacy full target");
    ok = sample(160, 160, 160, 160, OpaqueContent, OpaqueContent, AspectRatio{16, 9}, "inside Canvas") && ok;
    ok = sample(160, 5, 160, 5, Gutter, Gutter, AspectRatio{16, 9}, "top gutter") && ok;
    ok = sample(160, 315, 160, 315, Gutter, Gutter, AspectRatio{16, 9}, "bottom gutter") && ok;
    // One Engine frame, same Canvas, but independent native target aspects.
    if (!require(composition.resize(480, 240), "resize only composition target")) return false;
    ok = sample(160, 5, 5, 120, Gutter, Gutter, AspectRatio{16, 9}, "different-size outer gutters") && ok;
    ok = sample(5, 160, 60, 120, OpaqueContent, OpaqueContent, AspectRatio{16, 9}, "different-size Canvas interiors") && ok;
    ok = sample(160, 5, 5, 120, Gutter, Gutter, AspectRatio{4, 3}, "4:3 outer gutters") && ok;
    ok = sample(160, 160, 100, 120, OpaqueContent, OpaqueContent, AspectRatio{4, 3}, "4:3 Canvas interiors") && ok;
    ok = sample(160, 5, 5, 120, OpaqueContent, Gutter, AspectRatio{1, 1}, "matching aspect fills ordinary target") && ok;
    for (const auto invalid : {AspectRatio{0, 9}, AspectRatio{-16, 9}, AspectRatio{16, 0}, AspectRatio{-16, -9}})
        ok = sample(160, 160, 240, 120, Gutter, Gutter, invalid, "invalid explicit Canvas clear-only") && ok;
    ok = sample(160, 5, 5, 120, OpaqueContent, OpaqueContent, std::nullopt, "legacy restored after explicit invalid Canvas") && ok;
    ok = require(backend.presentationTargetInfo(a).ready && backend.presentationTargetInfo(b).ready,
        "authored invalidity never becomes native target failure") && ok;
    // Clip oversized Canvas-relative Layout, not just a unit-sized media quad.
    DesktopSceneEngine::Rendering::RenderData oversized{};
    oversized.canvasAspect = 16.0 / 9.0;
    oversized.presentation.destination = {-0.5F, -0.5F, 2.0F, 2.0F};
    if (!require(backend.beginFrame() == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable
            && backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(S_OK))
            && backend.captureNextPresentationPixelForTesting(a, 160, 5)
            && backend.captureNextPresentationPixelForTesting(b, 5, 120), "oversized Layout samples")) return false;
    backend.renderPrepared(oversized);
    ok = pixelMatches(backend, a, Gutter, "oversized Layout stays inside Canvas") && ok;
    ok = pixelMatches(backend, b, Gutter, "oversized Layout stays inside Canvas") && ok;
    // Native API facts above, never fitted dimensions, survive all Canvas changes.
    ok = require(backend.presentationTargetInfo(a).clientWidth == 320
            && backend.presentationTargetInfo(a).clientHeight == 320
            && backend.presentationTargetInfo(b).clientWidth == 480
            && backend.presentationTargetInfo(b).clientHeight == 240
            && backend.presentationTargetInfo(a).deviceGeneration == generation
            && backend.presentationTargetInfo(b).deviceGeneration == generation,
            "native dimensions remain independent of fitted viewport") && ok;
    const auto after = backend.compositionDiagnosticsForTesting();
    ok = require(after.capabilityIdentity == capability.capabilityIdentity
            && after.commitCount == capability.commitCount && after.waitCount == capability.waitCount,
            "Canvas geometry adds no capability, per-frame Commit or wait") && ok;
    return ok;
}

bool verifyNativeEdgesAndSuspension()
{
    TestWindow ordinary{101, 99, 100}, composition{101, 99, 500};
    Engine engine;
    const auto a = engine.initialize({ordinary.handle});
    const auto b = engine.attachPresentationTarget({composition.handle, PresentationTransport::DirectCompositionSwapChain});
    if (!require(a.isValid() && b.isValid() && engine.setPresentationPacingTarget(b), "edge test targets")) return false;
    auto& backend = CanvasTargetFitTestAccess::backend(engine);
    if (!require(backend.updateTexture(Content.data(), 1, 1, 4, 4, Content.size())
            == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated,
        "shared video-frame texture path")) return false;
    const auto paired = [&](int x, int y, AspectRatio aspect, const auto& expected)
    {
        if (!require(backend.beginFrame() == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable
                && backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(S_OK))
                && backend.captureNextPresentationPixelForTesting(a, x, y)
                && backend.captureNextPresentationPixelForTesting(b, x, y), "native edge samples")) return false;
        engine.render(aspect);
        bool ok = pixelMatches(backend, a, expected, "ordinary native Fit edge");
        return pixelMatches(backend, b, expected, "composition native Fit edge") && ok;
    };
    bool ok = paired(50, 20, {16, 9}, Gutter);
    ok = paired(50, 21, {16, 9}, OpaqueContent) && ok;
    // D3D11's implicit viewport scissor floors edge positions (spec 15.7).
    // Bottom 77.90625 becomes exclusive 77: one unavoidable pixel asymmetry.
    ok = paired(50, 76, {16, 9}, OpaqueContent) && ok;
    ok = paired(50, 77, {16, 9}, Gutter) && ok;
    ok = paired(50, 78, {16, 9}, Gutter) && ok;
    if (!require(ordinary.resize(160, 90) && composition.resize(160, 90), "resize to wide native targets")) return false;
    ok = paired(19, 45, {4, 3}, Gutter) && ok;
    ok = paired(20, 45, {4, 3}, OpaqueContent) && ok;
    ok = paired(139, 45, {4, 3}, OpaqueContent) && ok;
    ok = paired(140, 45, {4, 3}, Gutter) && ok;
    ok = paired(0, 0, {16, 9}, OpaqueContent) && ok;
    const auto beforeA = backend.presentationTargetDiagnosticsForTesting(a).drawCount;
    const auto beforeB = backend.presentationTargetDiagnosticsForTesting(b).drawCount;
    if (!require(ordinary.resize(0, 0)
            && backend.beginFrame() == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable
            && !backend.captureNextPresentationPixelForTesting(a)
            && backend.captureNextPresentationPixelForTesting(b, 80, 45), "zero-size target is non-drawable")) return false;
    engine.render({16, 9});
    ok = require(backend.presentationTargetInfo(a).suspended && backend.presentationTargetInfo(b).ready
            && backend.presentationTargetDiagnosticsForTesting(a).drawCount == beforeA
            && backend.presentationTargetDiagnosticsForTesting(b).drawCount == beforeB + 1,
            "suspended target cannot corrupt its usable peer") && ok;
    ok = pixelMatches(backend, b, OpaqueContent, "usable peer during suspension") && ok;
    if (!require(ordinary.resize(3, 11) && composition.resize(3, 11), "arbitrary matching ratio targets")) return false;
    ok = paired(2, 5, {3, 11}, OpaqueContent) && ok;
    if (!require(ordinary.resize(1, 1) && composition.resize(1, 1), "restore very small native targets")) return false;
    // Fit height 0.5625 has an empty implicit pixel extent on a 1x1 target.
    const auto tinyDraws = backend.presentationTargetDiagnosticsForTesting(a).drawCount;
    ok = paired(0, 0, {16, 9}, Gutter) && ok;
    ok = require(backend.presentationTargetInfo(a).ready
            && backend.presentationTargetDiagnosticsForTesting(a).drawCount == tinyDraws + 1,
            "subpixel Canvas remains drawable without stretching to force coverage") && ok;
    return require(backend.mediaUploadCountForTesting() == 1, "native transitions do not duplicate frame upload") && ok;
}

bool verifyInternalCanvasInput()
{
    TestWindow ordinary{160, 100, 100}, composition{160, 100, 500};
    GraphicsBackend backend;
    const auto a = backend.initialize({ordinary.handle, 10});
    const auto b = backend.attachPresentationTarget({composition.handle, 10, PresentationTransport::DirectCompositionSwapChain});
    if (!require(a.isValid() && b.isValid()
            && backend.updateTexture(Content.data(), 1, 1, 4, 4, Content.size())
                == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated,
        "internal neutral input targets")) return false;
    DesktopSceneEngine::Rendering::RenderData data{};
    data.clearColor[0] = 0; data.clearColor[1] = 0; data.clearColor[2] = 1; data.clearColor[3] = 0;
    constexpr std::array<unsigned char, 4> OpaqueBlue{0, 0, 255, 255};
    const auto sample = [&](int y, bool shouldDraw, const auto& expectedA, const auto& expectedB)
    {
        const auto draws = backend.presentationTargetDiagnosticsForTesting(a).drawCount;
        if (!require(backend.beginFrame() == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable
                && backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(S_OK))
                && backend.captureNextPresentationPixelForTesting(a, 80, y)
                && backend.captureNextPresentationPixelForTesting(b, 80, y), "internal input samples")) return false;
        backend.renderPrepared(data);
        bool ok = pixelMatches(backend, a, expectedA, "internal ordinary output");
        ok = pixelMatches(backend, b, expectedB, "internal composition output") && ok;
        return require(backend.presentationTargetInfo(a).ready && backend.presentationTargetInfo(b).ready
                && backend.presentationTargetDiagnosticsForTesting(a).drawCount == draws + (shouldDraw ? 1U : 0U),
                "invalid render input is clear-only, not native failure") && ok;
    };
    data.canvasAspect = 16.0 / 9.0;
    bool ok = sample(45, true, OpaqueContent, OpaqueContent);
    ok = sample(95, true, OpaqueBlue, OpaqueBlue) && ok; // Full RTV includes inset.
    for (const double invalid : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
    {
        data.canvasAspect = invalid;
        ok = sample(45, false, OpaqueBlue, OpaqueBlue) && ok;
    }
    data.canvasAspect.reset();
    ok = sample(45, true, OpaqueContent, OpaqueContent) && ok;
    data.canvasAspect = 16.0 / 9.0;
    data.canvasOutside = CanvasOutsideInstruction{b, CanvasOutsideMode::Transparent};
    data.presentation.destination = {0.25F, 0.25F, 0.5F, 0.5F};
    constexpr std::array<unsigned char, 4> Transparent{0, 0, 0, 0};
    ok = sample(5, true, OpaqueBlue, Transparent) && ok;
    ok = sample(45, true, OpaqueContent, OpaqueContent) && ok;
    ok = sample(95, true, OpaqueBlue, Transparent) && ok;
    data.canvasAspect = 0.0;
    ok = sample(45, false, OpaqueBlue, Transparent) && ok;
    return require(backend.presentationTargetInfo(a).clientHeight == 100
            && backend.presentationTargetInfo(a).viewportHeight == 90
            && backend.mediaUploadCountForTesting() == 1,
            "available native area and media ownership remain independent") && ok;
}

bool verifyCanvasOutsideFramePolicy()
{
    TestWindow ordinary{320, 320, 100}, composition{320, 320, 500}, peer{320, 320, 900};
    Engine engine;
    const auto a = engine.initialize({ordinary.handle});
    const auto b = engine.attachPresentationTarget({composition.handle,
        PresentationTransport::DirectCompositionSwapChain});
    const auto c = engine.attachPresentationTarget({peer.handle,
        PresentationTransport::DirectCompositionSwapChain});
    if (!require(a.isValid() && b.isValid() && c.isValid()
            && engine.setPresentationPacingTarget(b), "frame policy owns three real targets"))
        return false;
    auto& backend = CanvasTargetFitTestAccess::backend(engine);
    constexpr std::array<unsigned char, 4> Transparent{0, 0, 0, 0};
    const auto sample = [&](int x, int y, AspectRatio aspect,
        std::optional<CanvasOutsideInstruction> instruction,
        const auto& expectedA, const auto& expectedB, const auto& expectedC)
    {
        if (!require(backend.beginFrame()
                    == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable
                && backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(S_OK))
                && backend.captureNextPresentationPixelForTesting(a, x, y)
                && backend.captureNextPresentationPixelForTesting(b, x, y)
                && backend.captureNextPresentationPixelForTesting(c, x, y),
                "frame policy readback preconditions"))
            return false;
        engine.render(aspect, instruction);
        bool ok = pixelMatches(backend, a, expectedA, "unaddressed ordinary frame");
        ok = pixelMatches(backend, b, expectedB, "addressed composition frame") && ok;
        return pixelMatches(backend, c, expectedC, "unaddressed composition frame") && ok;
    };
    const CanvasOutsideInstruction transparent{b, CanvasOutsideMode::Transparent};
    const CanvasOutsideInstruction opaque{b, CanvasOutsideMode::Opaque};
    if (!sample(160, 5, {16, 9}, std::nullopt, Gutter, Gutter, Gutter)
        || !sample(160, 5, {16, 9}, transparent, Gutter, Transparent, Gutter)
        || !sample(160, 160, {16, 9}, transparent, Gutter, Transparent, Gutter)
        || !sample(160, 5, {16, 9}, opaque, Gutter, Gutter, Gutter))
        return false;
    constexpr std::array<unsigned char, 4> SourceBlack{0, 0, 0, 17};
    constexpr std::array<unsigned char, 4> OpaqueBlack{0, 0, 0, 255};
    if (!require(backend.updateImage(SourceBlack.data(), 1, 1, 4, 4, SourceBlack.size())
            == DesktopSceneEngine::Rendering::Detail::MediaUpdateResult::Updated,
            "source-alpha black fixture must upload once"))
        return false;
    if (!sample(160, 160, {16, 9}, transparent, OpaqueBlack, OpaqueBlack, OpaqueBlack)
        || !sample(160, 5, {16, 9}, transparent, Gutter, Transparent, Gutter))
        return false;
    if (!require(ordinary.resize(101, 99) && composition.resize(101, 99)
            && peer.resize(101, 99), "fractional policy viewport sizes"))
        return false;
    if (!sample(50, 20, {16, 9}, transparent, Gutter, Transparent, Gutter)
        || !sample(50, 21, {16, 9}, transparent, OpaqueBlack, OpaqueBlack, OpaqueBlack)
        || !sample(50, 76, {16, 9}, transparent, OpaqueBlack, OpaqueBlack, OpaqueBlack)
        || !sample(50, 77, {16, 9}, transparent, Gutter, Transparent, Gutter))
        return false;
    if (!require(ordinary.resize(160, 90) && composition.resize(160, 90)
            && peer.resize(160, 90), "integer policy viewport sizes"))
        return false;
    if (!sample(19, 45, {4, 3}, transparent, Gutter, Transparent, Gutter)
        || !sample(20, 45, {4, 3}, transparent, OpaqueBlack, OpaqueBlack, OpaqueBlack)
        || !sample(139, 45, {4, 3}, transparent, OpaqueBlack, OpaqueBlack, OpaqueBlack)
        || !sample(140, 45, {4, 3}, transparent, Gutter, Transparent, Gutter))
        return false;
    if (!require(ordinary.resize(1, 1) && composition.resize(1, 1)
            && peer.resize(1, 1), "tiny policy viewports"))
        return false;
    return sample(0, 0, {16, 9}, transparent, Gutter, Transparent, Gutter)
        && require(backend.presentationTargetInfo(b).ready
                && backend.mediaUploadCountForTesting() == 1,
            "tiny float coverage preserves native readiness and shared media upload");
}

// A single test-owned BMP exercises real Engine project/scene/media forwarding.
// No repository resource or second media pipeline is involved.
class ImageFixture
{
public:
    ImageFixture(int width = 4, int height = 4)
    {
        path = std::filesystem::temp_directory_path() / ("DSECanvasFit-"
            + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + ".bmp");
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        ownsFile = true;
        BITMAPFILEHEADER header{};
        header.bfType = 0x4D42;
        header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        const DWORD byteSize = static_cast<DWORD>(width * height * 4);
        header.bfSize = header.bfOffBits + byteSize;
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info); info.biWidth = width; info.biHeight = height;
        info.biPlanes = 1; info.biBitCount = 32; info.biSizeImage = byteSize;
        std::vector<unsigned char> pixels(byteSize);
        for (std::size_t i = 0; i < pixels.size(); i += 4)
        {
            const bool rightHalf = (i / 4) % width >= static_cast<std::size_t>(width / 2);
            pixels[i] = rightHalf ? 47 : 40;
            pixels[i + 1] = rightHalf ? 219 : 80;
            pixels[i + 2] = rightHalf ? 13 : 220;
            pixels[i + 3] = 255;
        }
        const auto write = [file](const void* bytes, DWORD length)
        { DWORD written = 0; return WriteFile(file, bytes, length, &written, nullptr) && written == length; };
        valid = write(&header, sizeof(header)) && write(&info, sizeof(info)) && write(pixels.data(), DWORD(pixels.size()));
        CloseHandle(file);
    }
    ~ImageFixture() { if (ownsFile) DeleteFileW(path.c_str()); }
    ImageFixture(const ImageFixture&) = delete;
    ImageFixture& operator=(const ImageFixture&) = delete;
    std::filesystem::path path;
    bool valid = false;
private:
    bool ownsFile = false;
};

bool verifyBoundSceneOutput()
{
    ImageFixture image;
    if (!require(image.valid, "real decoded image fixture")) return false;
    namespace Domain = DesktopSceneEngine::Domain;
    for (const auto mode : {Domain::MediaPresentationMode::Native,
            Domain::MediaPresentationMode::AutoFill, Domain::MediaPresentationMode::Custom})
    {
        TestWindow ordinary{320, 320, 100}, composition{320, 320, 500};
        Domain::Project project;
        Domain::Scene scene;
        scene.id = Domain::EntityId{std::string{"fit-scene"}};
        scene.mediaId = Domain::EntityId{std::string{"fit-media"}};
        scene.crop = {0.5F, 0, 0.5F, 1}; // Only the green half of the decoded source.
        scene.layout.scaleX = 0.5F; scene.layout.scaleY = 0.5F;
        scene.presentation = {mode, AspectRatio{1, 1}};
        project.library.media.push_back({scene.mediaId, "fit fixture", image.path.string()});
        project.scenes.push_back(scene);
        Engine engine;
        const auto a = engine.initialize({ordinary.handle});
        const auto b = engine.attachPresentationTarget({composition.handle, PresentationTransport::DirectCompositionSwapChain});
        if (!require(a.isValid() && b.isValid() && engine.bindProject(project, scene.id)
                && engine.currentMediaDimensions().has_value(), "real Engine scene binding")) return false;
        auto& backend = CanvasTargetFitTestAccess::backend(engine);
        constexpr std::array<unsigned char, 4> Green{13, 219, 47, 255};
        const auto sample = [&](int x, int y, const auto& expectedA, const auto& expectedB,
            std::optional<CanvasOutsideInstruction> instruction = std::nullopt)
        {
            if (!require(backend.beginFrame() == DesktopSceneEngine::Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable
                    && backend.forceNextDxgiPresentResultForTesting(a, static_cast<std::int32_t>(S_OK))
                    && backend.captureNextPresentationPixelForTesting(a, x, y)
                    && backend.captureNextPresentationPixelForTesting(b, x, y), "bound scene GPU samples")) return false;
            engine.render({16, 9}, instruction);
            bool ok = pixelMatches(backend, a, expectedA, "bound scene ordinary output");
            return pixelMatches(backend, b, expectedB, "bound scene composition output") && ok;
        };
        constexpr std::array<unsigned char, 4> Transparent{0, 0, 0, 0};
        const CanvasOutsideInstruction transparent{b, CanvasOutsideMode::Transparent};
        if (!sample(160, 160, Green, Green) || !sample(160, 5, Gutter, Gutter)
                || !sample(160, 88, Gutter, Gutter)
                || !sample(160, 5, Gutter, Transparent, transparent)
                || !sample(160, 88, Gutter, Transparent, transparent)
                || !sample(160, 160, Green, Green, transparent)
                || !require(backend.mediaUploadCountForTesting() == 1,
                    "Crop/Layout and all media modes retain one Engine media upload")) return false;
    }
    return true;
}

bool verifyContentCoveragePolicy(bool positioned)
{
    ImageFixture image{320, 200};
    TestWindow ordinary{320, 200, 100}, composition{320, 200, 460}, peer{320, 200, 820};
    namespace Domain = DesktopSceneEngine::Domain;
    Domain::Project project;
    Domain::Scene scene;
    scene.id = Domain::EntityId{std::string{"coverage-scene"}};
    scene.mediaId = Domain::EntityId{std::string{"coverage-media"}};
    scene.presentation.mode = Domain::MediaPresentationMode::Native;
    project.library.media.push_back({scene.mediaId, "coverage fixture", image.path.string()});
    project.scenes.push_back(scene);
    Engine engine;
    const auto a = engine.initialize({ordinary.handle});
    const auto b = engine.attachPresentationTarget({composition.handle, PresentationTransport::DirectCompositionSwapChain});
    const auto c = engine.attachPresentationTarget({peer.handle, PresentationTransport::DirectCompositionSwapChain});
    if (!require(image.valid && a.isValid() && b.isValid() && c.isValid()
            && engine.bindProject(project, scene.id) && engine.setPresentationPacingTarget(b),
            "real matching-aspect Engine content and three targets")) return false;
    auto& backend = CanvasTargetFitTestAccess::backend(engine);
    const auto info = backend.presentationTargetInfo(b);
    if (!require(info.clientWidth == 320 && info.viewportHeight == 200,
            "16:10 Canvas fills the 320x200 target; no outer Canvas gutters")) return false;
    constexpr std::array<unsigned char, 4> Transparent{0, 0, 0, 0};
    constexpr std::array<unsigned char, 4> Green{13, 219, 47, 255};
    constexpr std::array<unsigned char, 4> Red{220, 80, 40, 255};
    const auto sample = [&](int x, int y, CanvasOutsideMode mode,
        const auto& expectedMediaOrBackground, bool uncovered, const char* label)
    {
        if (backend.beginFrame() != DesktopSceneEngine::Rendering::Detail::FramePreparationResult::ReadyTargetsAvailable)
            return false;
        for (const auto id : {a, b, c})
            if (!backend.forceNextDxgiPresentResultForTesting(id, static_cast<std::int32_t>(S_OK))
                || !backend.captureNextPresentationPixelForTesting(id, x, y)) return false;
        engine.render({16, 10}, CanvasOutsideInstruction{b, mode});
        bool ok = pixelMatches(backend, a, expectedMediaOrBackground, "unchanged Preview pixels");
        ok = pixelMatches(backend, c, expectedMediaOrBackground, "unaddressed DComp peer pixels") && ok;
        return pixelMatches(backend, b,
            uncovered && mode == CanvasOutsideMode::Transparent ? Transparent : expectedMediaOrBackground,
            label) && ok;
    };
    if (!sample(80, 100, CanvasOutsideMode::Transparent, Red, false, "scale 1 Reveal covers Canvas")
        || !sample(240, 100, CanvasOutsideMode::Opaque, Green, false, "scale 1 Opaque same geometry")) return false;
    if (positioned)
    {
        project.scenes.front().layout.positionX = 0.2F;
        project.scenes.front().layout.positionY = 0.15F;
        // x=208 maps to source u~0.45 (red), not a re-fitted/clamped u~0.56 (green).
        if (!sample(208, 80, CanvasOutsideMode::Transparent, Red, false, "offset clipping preserves source UV")
            || !sample(280, 80, CanvasOutsideMode::Transparent, Green, false, "offset media remains opaque")
            || !sample(32, 80, CanvasOutsideMode::Opaque, Gutter, true, "offset Opaque background")) return false;
        bool ok = sample(32, 80, CanvasOutsideMode::Transparent, Gutter, true, "offset internal left gap must be transparent");
        ok = sample(200, 190, CanvasOutsideMode::Transparent, Gutter, true, "positive Y exposes bottom gap") && ok;
        project.scenes.front().layout.positionX = -0.2F;
        project.scenes.front().layout.positionY = -0.15F;
        ok = sample(288, 100, CanvasOutsideMode::Transparent, Gutter, true, "negative X exposes right gap") && ok;
        ok = sample(100, 10, CanvasOutsideMode::Transparent, Gutter, true, "negative Y exposes top gap") && ok;
        project.scenes.front().layout.positionX = 1.0F;
        project.scenes.front().layout.positionY = 0.0F;
        ok = sample(160, 100, CanvasOutsideMode::Transparent, Gutter, true, "fully clipped content leaves empty coverage") && ok;
        return require(backend.mediaUploadCountForTesting() == 1U, "offsets retain one shared media upload") && ok;
    }
    project.scenes.front().layout.scaleX = project.scenes.front().layout.scaleY = 0.9F;
    if (!sample(160, 100, CanvasOutsideMode::Transparent, Green, false, "scale 0.9 media center remains opaque")) return false;
    bool ok = true;
    for (const auto point : {std::array{8, 100}, std::array{312, 100}, std::array{160, 5}, std::array{160, 195}})
    {
        ok = sample(point[0], point[1], CanvasOutsideMode::Opaque, Gutter, true, "scale 0.9 Opaque border") && ok;
        ok = sample(point[0], point[1], CanvasOutsideMode::Transparent, Gutter, true, "scale 0.9 internal border must be transparent") && ok;
    }
    return require(backend.mediaUploadCountForTesting() == 1U, "three sinks share one media upload") && ok;
}

bool verifySameAspectContentCoveragePolicy() { return verifyContentCoveragePolicy(false); }
bool verifyPositionOffsetContentCoveragePolicy() { return verifyContentCoveragePolicy(true); }
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--red-uncovered-scale")
        return verifySameAspectContentCoveragePolicy() ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--red-uncovered-position")
        return verifyPositionOffsetContentCoveragePolicy() ? 0 : 1;
    bool ok = verifyEngineOutput();
    ok = verifySameAspectContentCoveragePolicy() && ok;
    ok = verifyPositionOffsetContentCoveragePolicy() && ok;
    ok = verifyCanvasOutsideFramePolicy() && ok;
    ok = verifyNativeEdgesAndSuspension() && ok;
    ok = verifyInternalCanvasInput() && ok;
    ok = verifyBoundSceneOutput() && ok;
    return ok ? 0 : 1;
}
