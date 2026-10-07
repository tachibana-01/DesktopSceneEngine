// Test-only static-image Engine measurement workload. No diagnostic setters,
// forced presentation results, GPU readback, Player, or desktop shell host.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dxgi1_2.h>

#include <DesktopSceneEngine/Engine.h>
#include <DesktopSceneEngine/Platform/Display.h>
#include "Rendering/Renderer.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>

namespace DesktopSceneEngine::Rendering
{
// Reuse the existing friend seam, but expose only a const observation here.
// All rendering/lifecycle mutations below use Engine's public API.
class CanvasTargetFitTestAccess
{
public:
    static const GraphicsBackend& backend(const Engine& engine)
    { return engine.renderer_->backend_; }
};
}

namespace
{
using namespace DesktopSceneEngine;
using Backend = Rendering::GraphicsBackend;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using Point = Clock::time_point;

double seconds(Point from, Point to) { return std::chrono::duration<double>(to - from).count(); }
std::string utcNow()
{
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char text[40]{};
    snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return text;
}
std::string utf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
std::int64_t performanceCounter()
{
    LARGE_INTEGER value{};
    require(QueryPerformanceCounter(&value) != FALSE, "QueryPerformanceCounter failed");
    return value.QuadPart;
}
std::int64_t performanceFrequency()
{
    LARGE_INTEGER value{};
    require(QueryPerformanceFrequency(&value) != FALSE && value.QuadPart > 0, "QueryPerformanceFrequency failed");
    return value.QuadPart;
}
Json measurementBoundary(Point origin, Point renderEnd, std::int64_t counter)
{
    return {{"counterTicks", counter}, {"harnessElapsedSeconds", seconds(origin, renderEnd)}};
}

struct Options
{
    std::filesystem::path media, output;
    double warmup = 60.0, warmupTimeout = 180.0, measure = 120.0, periodMs = 16.0;
};
Options parse(int argc, wchar_t** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring name = argv[i];
        require(i + 1 < argc, "missing CLI value");
        const std::wstring value = argv[++i];
        if (name == L"--media") options.media = value;
        else if (name == L"--output-dir") options.output = value;
        else
        {
            // wcstod obeys the unchanged C numeric locale; reject trailing text.
            wchar_t* end = nullptr;
            const double number = wcstod(value.c_str(), &end);
            require(end != value.c_str() && *end == L'\0' && std::isfinite(number) && number > 0,
                "CLI number must be finite and positive (decimal point)");
            if (name == L"--warmup-seconds") options.warmup = number;
            else if (name == L"--warmup-timeout-seconds") options.warmupTimeout = number;
            else if (name == L"--measure-seconds") options.measure = number;
            else if (name == L"--frame-period-ms") options.periodMs = number;
            else throw std::runtime_error("unknown CLI option");
        }
    }
    require(!options.media.empty() && !options.output.empty(), "--media and --output-dir are required");
    require(options.warmup >= 60 && options.warmupTimeout >= options.warmup, "warmup minimum is 60 seconds");
    require(options.periodMs >= 1 && options.periodMs <= 100, "frame period must be 1..100 ms");
    require(std::filesystem::is_regular_file(options.media), "media file unavailable");
    require(std::string(SOAK_BUILD_CONFIGURATION) == "Release", "qualification requires a Release build");
    return options;
}

struct StopRequest { bool requested = false; };
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
    if (message == WM_CLOSE)
    {
        if (auto* stop = reinterpret_cast<StopRequest*>(GetWindowLongPtrW(window, GWLP_USERDATA)))
            stop->requested = true;
        // Borrowed HWND must survive until Engine detaches/releases resources.
        return 0;
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wparam, lparam);
}
struct Windows
{
    StopRequest stop;
    HWND ordinary = nullptr, composition = nullptr;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    DPI_AWARENESS_CONTEXT previousDpi = nullptr;
    bool registered = false;
    static constexpr wchar_t className[] = L"DSEEngineGraphicsSoakWindow";
    Windows() = default;
    Windows(const Windows&) = delete;
    ~Windows() { destroy(); }
    void create()
    {
        previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        require(previousDpi != nullptr, "cannot set thread per-monitor DPI awareness");
        WNDCLASSW type{};
        type.lpfnWndProc = windowProcedure;
        type.hInstance = instance;
        type.lpszClassName = className;
        registered = RegisterClassW(&type) != 0;
        require(registered, "window class registration failed");
        MONITORINFO monitor{sizeof(MONITORINFO)};
        const HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        require(GetMonitorInfoW(primary, &monitor) != FALSE, "primary monitor unavailable");
        const auto& area = monitor.rcWork;
        const LONG width = area.right - area.left, height = area.bottom - area.top;
        const bool horizontal = width >= 1300 && height >= 380;
        require(horizontal || (width >= 660 && height >= 740), "primary work area cannot fit two 640x360 windows");
        const int x = area.left + 10, y = area.top + 10;
        ordinary = CreateWindowExW(WS_EX_NOACTIVATE, className, L"DSE Soak: ordinary HWND",
            WS_POPUP, x, y, 640, 360, nullptr, nullptr, instance, &stop);
        composition = CreateWindowExW(WS_EX_NOACTIVATE, className, L"DSE Soak: DirectComposition HWND",
            WS_POPUP, horizontal ? x + 650 : x, horizontal ? y : y + 370,
            640, 360, nullptr, nullptr, instance, &stop);
        require(ordinary && composition, "test HWND creation failed");
        ShowWindow(ordinary, SW_SHOWNOACTIVATE);
        ShowWindow(composition, SW_SHOWNOACTIVATE);
        UpdateWindow(ordinary); UpdateWindow(composition);
        require(valid(ordinary) && valid(composition)
            && MonitorFromWindow(ordinary, MONITOR_DEFAULTTONULL) == primary
            && MonitorFromWindow(composition, MONITOR_DEFAULTTONULL) == primary,
            "test HWND visibility/client-size/primary-output precondition failed");
    }
    static bool valid(HWND handle)
    {
        RECT client{};
        return IsWindow(handle) && IsWindowVisible(handle) && GetClientRect(handle, &client)
            && client.right - client.left == 640 && client.bottom - client.top == 360;
    }
    void pump()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT) stop.requested = true;
            else { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    }
    void waitUntil(Point deadline)
    {
        while (!stop.requested && Clock::now() < deadline)
        {
            const double remaining = seconds(Clock::now(), deadline) * 1000;
            const DWORD delay = static_cast<DWORD>(std::max(1.0, std::ceil(remaining)));
            MsgWaitForMultipleObjectsEx(0, nullptr, delay, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            pump();
        }
    }
    bool destroy() noexcept
    {
        bool ok = true;
        if (composition && IsWindow(composition)) ok = DestroyWindow(composition) != FALSE && ok;
        if (ordinary && IsWindow(ordinary)) ok = DestroyWindow(ordinary) != FALSE && ok;
        composition = ordinary = nullptr;
        if (registered) { ok = UnregisterClassW(className, instance) != FALSE && ok; registered = false; }
        if (previousDpi) { SetThreadDpiAwarenessContext(previousDpi); previousDpi = nullptr; }
        return ok;
    }
};

const char* state(const Backend::PresentationTargetInfo& info)
{
    if (info.failed) return "Failed";
    if (info.suspended) return "Suspended";
    if (info.occluded) return "Occluded";
    return info.ready ? "Ready" : "Unavailable";
}
const char* trust(Backend::CompositionDiagnostics::TrustState value)
{
    using Trust = Backend::CompositionDiagnostics::TrustState;
    switch (value)
    {
    case Trust::Trusted: return "Trusted";
    case Trust::UntrustedPending: return "UntrustedPending";
    case Trust::UntrustedSpent: return "UntrustedSpent";
    default: return "Unavailable";
    }
}
struct Snapshot
{
    Backend::PresentationTargetInfo ordinary, composition;
    Backend::PresentationTargetDiagnostics ordinaryCounters, compositionCounters;
    Backend::CompositionDiagnostics shared;
    std::uint64_t uploads = 0, creations = 0, destructions = 0, recoveries = 0;
    std::size_t targetCount = 0;
    bool media = false, generation = false, recoveryPending = false, ordinaryPending = false, compositionPending = false;
    std::string preferred, actualPacingSubmission = "none";
};
Snapshot observe(const Backend& backend, PresentationTargetId ordinary, PresentationTargetId composition)
{
    Snapshot value;
    value.ordinary = backend.presentationTargetInfo(ordinary);
    value.composition = backend.presentationTargetInfo(composition);
    value.ordinaryCounters = backend.presentationTargetDiagnosticsForTesting(ordinary);
    value.compositionCounters = backend.presentationTargetDiagnosticsForTesting(composition);
    value.shared = backend.compositionDiagnosticsForTesting();
    value.uploads = backend.mediaUploadCountForTesting();
    value.creations = backend.graphicsGenerationCreationCountForTesting();
    value.destructions = backend.graphicsGenerationDestructionCountForTesting();
    value.recoveries = backend.graphicsRecoveryAttemptCountForTesting();
    value.targetCount = backend.presentationTargetCountForTesting();
    value.media = backend.hasCurrentMediaRealizationForTesting();
    value.generation = backend.hasCurrentGraphicsGenerationForTesting();
    value.recoveryPending = backend.pendingGraphicsRecoveryForTesting();
    value.ordinaryPending = backend.pendingTargetLocalRecoveryForTesting(ordinary);
    value.compositionPending = backend.pendingTargetLocalRecoveryForTesting(composition);
    const auto label = [&](PresentationTargetId id) {
        return id == ordinary ? "ordinary" : id == composition ? "composition" : "none";
    };
    value.preferred = label(backend.preferredPacingTargetForTesting());
    return value;
}
Json snapshotJson(const Snapshot& value)
{
    return {{"ordinaryState", state(value.ordinary)}, {"compositionState", state(value.composition)},
        {"deviceGeneration", value.ordinary.deviceGeneration}, {"ordinaryGeneration", value.ordinary.targetGeneration},
        {"compositionGeneration", value.composition.targetGeneration}, {"uploads", value.uploads},
        {"mediaRealized", value.media}, {"targetCount", value.targetCount},
        {"compositionTrust", trust(value.shared.trustState)}, {"capabilityIdentity", value.shared.capabilityIdentity},
        {"capabilityCreations", value.shared.capabilityCreationCount}, {"commits", value.shared.commitCount},
        {"waits", value.shared.waitCount}, {"graphicsCreations", value.creations},
        {"graphicsDestructions", value.destructions}, {"recoveryAttempts", value.recoveries},
        {"ordinaryDraws", value.ordinaryCounters.drawCount}, {"ordinaryPresentAttempts", value.ordinaryCounters.presentAttemptCount},
        {"compositionDraws", value.compositionCounters.drawCount}, {"compositionPresentAttempts", value.compositionCounters.presentAttemptCount},
        {"ordinaryResizes", value.ordinaryCounters.successfulResizeCount},
        {"compositionResizes", value.compositionCounters.successfulResizeCount},
        {"preferredPacing", value.preferred}, {"actualPacingSubmission", value.actualPacingSubmission}};
}
bool sameResourceEpoch(const Snapshot& a, const Snapshot& b)
{
    return a.ordinary.deviceGeneration == b.ordinary.deviceGeneration
        && a.composition.deviceGeneration == b.composition.deviceGeneration
        && a.ordinary.targetGeneration == b.ordinary.targetGeneration
        && a.composition.targetGeneration == b.composition.targetGeneration
        && a.uploads == b.uploads && a.shared.capabilityIdentity == b.shared.capabilityIdentity
        && a.shared.capabilityCreationCount == b.shared.capabilityCreationCount
        && a.shared.commitCount == b.shared.commitCount && a.shared.waitCount == b.shared.waitCount
        && a.creations == b.creations && a.destructions == b.destructions && a.recoveries == b.recoveries
        && a.ordinaryCounters.successfulResizeCount == b.ordinaryCounters.successfulResizeCount
        && a.compositionCounters.successfulResizeCount == b.compositionCounters.successfulResizeCount;
}

bool currentPresentAttempt(const Backend::PresentationTargetInfo& previousInfo,
    const Backend::PresentationTargetInfo& currentInfo,
    const Backend::PresentationTargetDiagnostics& previous,
    const Backend::PresentationTargetDiagnostics& current)
{
    const bool advanced = previousInfo.deviceGeneration == currentInfo.deviceGeneration
        && previousInfo.targetGeneration == currentInfo.targetGeneration
        && current.presentAttemptCount > previous.presentAttemptCount;
    if (advanced) require(current.presentAttemptCount - previous.presentAttemptCount == 1,
        "multiple attempts for one target in a single frame");
    return advanced;
}
std::string actualPacingSubmission(const Snapshot& previous, const Snapshot& current)
{
    // Readiness observed after Present may already be Occluded. Only this
    // frame's same-generation attempt delta makes last-Present metadata current.
    const auto paced = [](bool attempted, const Backend::PresentationTargetDiagnostics& value) {
        if (!attempted) return false;
        require((value.lastPresentSyncInterval == 1 && value.lastPresentFlags == 0)
            || (value.lastPresentSyncInterval == 0 && value.lastPresentFlags == DXGI_PRESENT_DO_NOT_WAIT),
            "unexpected current presentation pacing metadata");
        return value.lastPresentSyncInterval == 1;
    };
    const bool ordinary = paced(currentPresentAttempt(previous.ordinary, current.ordinary,
        previous.ordinaryCounters, current.ordinaryCounters), current.ordinaryCounters);
    const bool composition = paced(currentPresentAttempt(previous.composition, current.composition,
        previous.compositionCounters, current.compositionCounters), current.compositionCounters);
    require(!(ordinary && composition), "multiple current pacing submissions");
    return ordinary ? "ordinary" : composition ? "composition" : "none";
}
void testPacingObservation()
{
    // Synthetic observations only: no Engine, HWND, or production fault control.
    Snapshot previous;
    previous.preferred = "composition";
    previous.ordinary.ready = previous.composition.ready = true;
    previous.ordinary.deviceGeneration = previous.composition.deviceGeneration = 1;
    previous.ordinary.targetGeneration = previous.composition.targetGeneration = 1;
    previous.ordinaryCounters.presentAttemptCount = previous.compositionCounters.presentAttemptCount = 10;
    previous.compositionCounters.lastPresentSyncInterval = 1;
    auto dualReady = previous;
    ++dualReady.ordinaryCounters.presentAttemptCount; ++dualReady.compositionCounters.presentAttemptCount;
    dualReady.ordinaryCounters.lastPresentFlags = DXGI_PRESENT_DO_NOT_WAIT;
    require(actualPacingSubmission(previous, dualReady) == "composition", "dual Ready pacing observation");
    auto fallback = dualReady;
    fallback.composition.ready = false; fallback.composition.occluded = true;
    fallback.compositionCounters.presentAttemptCount = previous.compositionCounters.presentAttemptCount;
    fallback.ordinaryCounters.lastPresentSyncInterval = 1; fallback.ordinaryCounters.lastPresentFlags = 0;
    require(fallback.preferred == "composition" && actualPacingSubmission(previous, fallback) == "ordinary",
        "preferred unavailable must observe ordinary pacing submission");
    require(actualPacingSubmission(previous, previous) == "none", "no current attempt must observe no pacer");
    auto stale = previous;
    ++stale.ordinaryCounters.presentAttemptCount; stale.ordinaryCounters.lastPresentFlags = DXGI_PRESENT_DO_NOT_WAIT;
    require(actualPacingSubmission(previous, stale) == "none", "stale last-Present metadata must not pace");
    auto newEpoch = previous;
    ++newEpoch.composition.targetGeneration; ++newEpoch.compositionCounters.presentAttemptCount;
    require(actualPacingSubmission(previous, newEpoch) == "none", "new target epoch has no comparable attempt delta");
    newEpoch = previous; ++newEpoch.composition.deviceGeneration; ++newEpoch.compositionCounters.presentAttemptCount;
    require(actualPacingSubmission(previous, newEpoch) == "none", "new D3D generation has no comparable attempt delta");
    auto reset = previous; reset.compositionCounters.presentAttemptCount = 0;
    require(actualPacingSubmission(previous, reset) == "none", "counter reset must not reuse old metadata");
    auto multipleAttempts = previous; multipleAttempts.compositionCounters.presentAttemptCount += 2;
    bool rejected = false;
    try { (void)actualPacingSubmission(previous, multipleAttempts); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "multiple attempts must not attribute stale last-Present metadata to the whole frame");
    auto contradictory = dualReady;
    contradictory.ordinaryCounters.lastPresentSyncInterval = 1; contradictory.ordinaryCounters.lastPresentFlags = 0;
    rejected = false;
    try { (void)actualPacingSubmission(previous, contradictory); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "two pacing submissions must invalidate the observation");
    std::cout << Json{{"result", "PASS"}, {"checks", {"dual Ready", "preferred unavailable / ordinary fallback",
        "no attempt", "stale metadata", "target generation change", "D3D generation change",
        "counter reset", "multiple attempts rejected", "contradictory pacing rejected"}}}.dump(2) << '\n';
}

struct Outputs
{
    std::filesystem::path directory;
    std::ofstream frames, events;
    std::uint64_t heartbeatReplacementDeferrals = 0;
    DWORD lastHeartbeatReplacementError = ERROR_SUCCESS;
    explicit Outputs(const std::filesystem::path& path) : directory(path)
    {
        std::filesystem::create_directories(path);
        // Never silently overwrite earlier harness evidence.
        for (const auto* name : {L"frames.csv", L"harness-events.jsonl", L"heartbeat.json", L"harness-result.json"})
            require(!std::filesystem::exists(path / name), "harness output already exists");
        frames.open(path / L"frames.csv"); events.open(path / L"harness-events.jsonl");
        require(frames.is_open() && events.is_open(), "cannot open harness output streams");
        frames.exceptions(std::ios::badbit | std::ios::failbit);
        events.exceptions(std::ios::badbit | std::ios::failbit);
        frames.imbue(std::locale::classic()); frames << std::setprecision(12);
        frames << "sequence,phase,elapsedSeconds,utc,intendedDeadlineSeconds,frameIntervalMs,latenessMs,suppliedDtSeconds,updateMs,renderMs,runtimeSeconds,paused,speed,ordinaryId,compositionId,ordinaryState,compositionState,compositionTrust,ordinaryDraws,ordinaryPresentAttempts,compositionDraws,compositionPresentAttempts,deviceGeneration,compositionDeviceGeneration,ordinaryGeneration,compositionGeneration,uploads,mediaRealized,targetCount,preferredPacing,actualPacingSubmission,graphicsCreations,graphicsDestructions,recoveryAttempts,recoveryPending,ordinaryRecoveryPending,compositionRecoveryPending,capabilityIdentity,capabilityCreations,commits,waits,ordinaryResizes,compositionResizes,ordinaryClientWidth,ordinaryClientHeight,compositionClientWidth,compositionClientHeight,ordinaryPresentCurrent,compositionPresentCurrent,ordinarySyncInterval,ordinaryPresentFlags,compositionSyncInterval,compositionPresentFlags,ordinaryCounterEpoch,compositionCounterEpoch,totalWorkMs\n";
    }
    void event(const char* type, double elapsed, Json facts = Json::object())
    {
        events << Json{{"utc", utcNow()}, {"elapsedSeconds", elapsed}, {"event", type}, {"facts", std::move(facts)}}.dump() << '\n';
        events.flush();
    }
    bool heartbeat(const Json& value)
    {
        const auto temporary = directory / L"heartbeat.json.tmp";
        {
            std::ofstream file(temporary);
            file.exceptions(std::ios::badbit | std::ios::failbit);
            file << value.dump() << '\n'; file.flush();
        }
        if (MoveFileExW(temporary.c_str(), (directory / L"heartbeat.json").c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) return true;
        lastHeartbeatReplacementError = GetLastError();
        if (lastHeartbeatReplacementError == ERROR_ACCESS_DENIED
            || lastHeartbeatReplacementError == ERROR_SHARING_VIOLATION)
        {
            // Windows can deny replacement during a concurrent reader's open
            // interval. Preserve the previous complete heartbeat; retry on the
            // next scheduled frame, without sleeping or changing Engine state.
            // A persistent publication failure is caught by the collector's
            // unchanged 30-second stale-heartbeat deadline.
            ++heartbeatReplacementDeferrals;
            return false;
        }
        throw std::runtime_error("heartbeat replacement failed, Win32="
            + std::to_string(lastHeartbeatReplacementError));
    }
    void result(const Json& value)
    {
        frames.flush(); events.flush();
        std::ofstream file(directory / L"harness-result.json");
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file << value.dump(2) << '\n'; file.flush();
    }
};

Json windowJson(HWND handle)
{
    RECT client{}, screen{}; GetClientRect(handle, &client); GetWindowRect(handle, &screen);
    return {{"hwnd", reinterpret_cast<std::uintptr_t>(handle)}, {"dpi", GetDpiForWindow(handle)},
        {"clientWidth", client.right}, {"clientHeight", client.bottom},
        {"x", screen.left}, {"y", screen.top}, {"width", screen.right-screen.left}, {"height", screen.bottom-screen.top}};
}
Json displayJson()
{
    const auto enumeration = Platform::enumerateActiveDisplays();
    Json displays = Json::array();
    for (const auto& display : enumeration.snapshot.displays)
    {
        Json row{{"id", display.id.canonicalRepresentation().value_or("NA")},
            {"name", display.friendlyName}, {"source", display.sourceDeviceName}, {"primary", display.primary},
            {"x", display.desktopX}, {"y", display.desktopY}, {"width", display.pixelWidth}, {"height", display.pixelHeight}};
        if (display.refreshRate) row["refreshRate"] = {{"numerator", display.refreshRate->numerator}, {"denominator", display.refreshRate->denominator}};
        displays.push_back(std::move(row));
    }
    return {{"enumerationStatus", static_cast<int>(enumeration.status)}, {"displays", std::move(displays)}};
}

int run(const Options& options)
{
    Outputs output(options.output);
    const Point origin = Clock::now();
    Json measurementWindow{{"timebase", "QueryPerformanceCounter"}, {"frequency", performanceFrequency()},
        {"endpoints", "inclusive"}, {"start", nullptr}, {"end", nullptr},
        {"boundaryDefinition", "shared QPC stamp immediately after Engine.render completion; UTC/heartbeat phase not selection authority"}};
    const std::string startUtc = utcNow();
    const auto elapsed = [&] { return seconds(origin, Clock::now()); };
    output.event("start", elapsed(), {{"configuration", SOAK_BUILD_CONFIGURATION}, {"pid", GetCurrentProcessId()},
        {"media", utf8(options.media)}, {"warmupSeconds", options.warmup}, {"warmupTimeoutSeconds", options.warmupTimeout},
        {"measureSeconds", options.measure}, {"framePeriodMs", options.periodMs}, {"displaySnapshot", displayJson()},
        {"faultInjection", "NONE"}, {"canvas", "16:9"}, {"logging", "production logging unchanged; stdout/stderr captured externally"}});

    // Declaration order: Engine dies before its borrowed Project and HWNDs.
    Windows windows;
    Domain::Project project;
    Engine engine;
    PresentationTargetId ordinaryId, compositionId;
    std::uint64_t ordinaryValue = 0, compositionValue = 0;
    std::uint64_t sequence = 0, warmupFrames = 0, measurementFrames = 0, resourceChanges = 0, stateChanges = 0;
    std::uint64_t ordinaryEpoch = 0, compositionEpoch = 0;
    bool measurementStarted = false, completed = false, cleanShutdown = false;
    bool ordinaryActivity = true, compositionActivity = true;
    double warmupElapsed = 0, measuredElapsed = 0, runtimeStart = 0, runtimeEnd = 0, suppliedSum = 0;
    double maxRuntimeStall = 0;
    Snapshot initial, measurementStart, last;
    std::string failure;
    try
    {
        windows.create();
        output.event("windows_created", elapsed(), {{"ordinary", windowJson(windows.ordinary)}, {"composition", windowJson(windows.composition)}});
        ordinaryId = engine.initialize({windows.ordinary, PresentationTransport::HwndSwapChain});
        require(engine.isInitialized() && ordinaryId.isValid(), "ordinary Engine initialization failed");
        const Backend& backend = Rendering::CanvasTargetFitTestAccess::backend(engine);
        // Read-only allocation diagnostics supply numeric labels; IDs themselves
        // remain opaque and all calls use the exact returned Engine ID tokens.
        ordinaryValue = backend.nextPresentationTargetValueForTesting() - 1;
        compositionValue = backend.nextPresentationTargetValueForTesting();
        const auto expectedComposition = backend.nextPresentationTargetIdForTesting();
        output.event("engine_initialized", elapsed(), {{"ordinaryId", ordinaryValue}});
        compositionId = engine.attachPresentationTarget({windows.composition, PresentationTransport::DirectCompositionSwapChain});
        require(compositionId.isValid() && compositionId == expectedComposition && compositionId != ordinaryId,
            "DirectComposition target attachment failed");
        require(engine.setPresentationPacingTarget(compositionId), "DirectComposition preferred pacing selection failed");
        output.event("targets_attached", elapsed(), {{"ordinaryId", ordinaryValue}, {"compositionId", compositionValue},
            {"ordinaryTransport", "HwndSwapChain"}, {"compositionTransport", "DirectCompositionSwapChain"}});
        const Domain::EntityId mediaId{"soak-static-image"}, sceneId{"soak-static-scene"};
        project.library.media.push_back({mediaId, "frozen JPEG", utf8(options.media)});
        Domain::Scene scene; scene.id = sceneId; scene.mediaId = mediaId; scene.duration = 0.0F;
        scene.playback.speed = 1.0F; project.scenes.push_back(scene);
        require(engine.bindProject(project, sceneId), "Engine static-image project bind failed");
        const auto dimensions = engine.currentMediaDimensions();
        require(engine.mediaKind() == Engine::MediaKind::Image && dimensions
            && dimensions->width == 1920 && dimensions->height == 1080, "fixture dimensions/media kind mismatch");
        initial = last = observe(backend, ordinaryId, compositionId);
        output.event("media_bound", elapsed(), snapshotJson(initial));
        output.event("warmup_start", elapsed());

        const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(options.periodMs));
        const Point loopStart = Clock::now();
        Point previousStart = loopStart, deadline = loopStart, stableSince = loopStart, heartbeatAt = loopStart, frameFlushAt = loopStart;
        Point measureAt{}, lastClockProgress = loopStart;
        double previousRuntime = engine.playbackTime();
        Snapshot activityWindowStart = initial;
        Point activityWindowAt = loopStart;
        bool recentOrdinaryActivity = false;

        while (!completed)
        {
            windows.waitUntil(deadline); windows.pump();
            require(!windows.stop.requested, "external close/stop before intended measurement completion");
            require(Windows::valid(windows.ordinary) && Windows::valid(windows.composition), "test HWND unavailable or client size changed");
            const Point frameStart = Clock::now();
            const double interval = seconds(previousStart, frameStart);
            const float dt = static_cast<float>(std::min(interval, 0.1));
            previousStart = frameStart;
            const Point updateStart = Clock::now(); engine.update(dt); const Point updateEnd = Clock::now();
            engine.render(Domain::AspectRatio{16, 9}); const Point renderEnd = Clock::now();
            const auto renderEndCounter = performanceCounter();
            Snapshot current = observe(backend, ordinaryId, compositionId);
            current.actualPacingSubmission = actualPacingSubmission(last, current);
            bool heartbeatWritten = false;
            const double runtime = engine.playbackTime();
            const double stall = seconds(lastClockProgress, renderEnd);
            maxRuntimeStall = std::max(maxRuntimeStall, stall);
            require(std::isfinite(runtime) && runtime >= previousRuntime && !engine.isPaused() && engine.playbackSpeed() == 1.0F,
                "Runtime nonfinite/reset/pause/speed change");
            if (runtime > previousRuntime) lastClockProgress = renderEnd;
            require(stall <= 30.0, "Runtime stopped for more than 30 seconds");
            require(current.targetCount == 2 && current.generation && current.media, "required targets/graphics generation/media realization unavailable");
            require(current.ordinary.ready && !current.composition.failed,
                "required ordinary target unavailable or DirectComposition target Failed");
            require(current.preferred == "composition", "preferred pacing target changed");
            require(current.shared.armedFailurePoint == Rendering::Detail::CompositionFailurePoint::None
                && current.shared.armedRollbackFailurePoint == Rendering::Detail::CompositionFailurePoint::None,
                "unexpected deterministic D3D error-path test control armed");
            if (!sameResourceEpoch(last, current))
            {
                ++resourceChanges; stableSince = renderEnd;
                output.event("resource_epoch_change", elapsed(), {{"previous", snapshotJson(last)}, {"current", snapshotJson(current)}});
                if (measurementStarted) throw std::runtime_error("fixed measurement resource/target/upload epoch changed");
                require(resourceChanges <= 1, "repeated resource/generation churn in warmup");
            }
            if (std::string(state(last.ordinary)) != state(current.ordinary)
                || std::string(state(last.composition)) != state(current.composition)
                || last.shared.trustState != current.shared.trustState
                || last.actualPacingSubmission != current.actualPacingSubmission)
            {
                ++stateChanges;
                output.event("presentation_state_change", elapsed(), snapshotJson(current));
            }
            if (current.ordinary.deviceGeneration != last.ordinary.deviceGeneration
                || current.ordinary.targetGeneration != last.ordinary.targetGeneration
                || current.ordinaryCounters.presentAttemptCount < last.ordinaryCounters.presentAttemptCount) ++ordinaryEpoch;
            if (current.composition.deviceGeneration != last.composition.deviceGeneration
                || current.composition.targetGeneration != last.composition.targetGeneration
                || current.compositionCounters.presentAttemptCount < last.compositionCounters.presentAttemptCount) ++compositionEpoch;
            const bool ordinaryCurrent = currentPresentAttempt(last.ordinary, current.ordinary,
                last.ordinaryCounters, current.ordinaryCounters);
            const bool compositionCurrent = currentPresentAttempt(last.composition, current.composition,
                last.compositionCounters, current.compositionCounters);
            const char* phase = measurementStarted ? "measurement" : "warmup";
            ++sequence;
            if (measurementStarted) { ++measurementFrames; suppliedSum += dt; runtimeEnd = runtime; measuredElapsed = seconds(measureAt, renderEnd); }
            else { ++warmupFrames; warmupElapsed = seconds(loopStart, renderEnd); }
            auto& csv = output.frames;
            csv << sequence << ',' << phase << ',' << seconds(origin, frameStart) << ',' << utcNow() << ','
                << seconds(origin, deadline) << ',' << interval*1000 << ',' << std::max(0.0, seconds(deadline, frameStart)*1000) << ',' << dt << ','
                << seconds(updateStart, updateEnd)*1000 << ',' << seconds(updateEnd, renderEnd)*1000 << ','
                << runtime << ',' << engine.isPaused() << ',' << engine.playbackSpeed() << ',' << ordinaryValue << ',' << compositionValue << ','
                << state(current.ordinary) << ',' << state(current.composition) << ',' << trust(current.shared.trustState) << ','
                << current.ordinaryCounters.drawCount << ',' << current.ordinaryCounters.presentAttemptCount << ','
                << current.compositionCounters.drawCount << ',' << current.compositionCounters.presentAttemptCount << ','
                << current.ordinary.deviceGeneration << ',' << current.composition.deviceGeneration << ','
                << current.ordinary.targetGeneration << ',' << current.composition.targetGeneration << ','
                << current.uploads << ',' << current.media << ',' << current.targetCount << ',' << current.preferred << ',' << current.actualPacingSubmission << ','
                << current.creations << ',' << current.destructions << ',' << current.recoveries << ',' << current.recoveryPending << ','
                << current.ordinaryPending << ',' << current.compositionPending << ',' << current.shared.capabilityIdentity << ','
                << current.shared.capabilityCreationCount << ',' << current.shared.commitCount << ',' << current.shared.waitCount << ','
                << current.ordinaryCounters.successfulResizeCount << ',' << current.compositionCounters.successfulResizeCount << ','
                << current.ordinary.clientWidth << ',' << current.ordinary.clientHeight << ',' << current.composition.clientWidth << ',' << current.composition.clientHeight << ','
                << ordinaryCurrent << ',' << compositionCurrent << ',';
            if (ordinaryCurrent) csv << current.ordinaryCounters.lastPresentSyncInterval << ',' << current.ordinaryCounters.lastPresentFlags;
            else csv << "NA,NA";
            csv << ',';
            if (compositionCurrent) csv << current.compositionCounters.lastPresentSyncInterval << ',' << current.compositionCounters.lastPresentFlags;
            else csv << "NA,NA";
            csv << ',' << ordinaryEpoch << ',' << compositionEpoch;

            if (seconds(activityWindowAt, renderEnd) >= 20.0)
            {
                const bool ordinaryWindow = current.ordinaryCounters.drawCount > activityWindowStart.ordinaryCounters.drawCount
                    && current.ordinaryCounters.presentAttemptCount > activityWindowStart.ordinaryCounters.presentAttemptCount;
                const bool compositionWindow = current.compositionCounters.drawCount > activityWindowStart.compositionCounters.drawCount
                    && current.compositionCounters.presentAttemptCount > activityWindowStart.compositionCounters.presentAttemptCount;
                recentOrdinaryActivity = ordinaryWindow;
                if (measurementStarted) { ordinaryActivity = ordinaryActivity && ordinaryWindow; compositionActivity = compositionActivity && compositionWindow; }
                activityWindowStart = current; activityWindowAt = renderEnd;
            }
            if (seconds(heartbeatAt, renderEnd) >= 1.0)
            {
                if (output.heartbeat({{"utc", utcNow()}, {"phase", phase}, {"sequence", sequence}, {"elapsedSeconds", elapsed()},
                    {"runtimeSeconds", runtime}, {"lastUpdateSequence", sequence}, {"lastRenderSequence", sequence},
                    {"ordinaryCounterEpoch", ordinaryEpoch}, {"compositionCounterEpoch", compositionEpoch},
                    {"ordinaryHwnd", reinterpret_cast<std::uintptr_t>(windows.ordinary)},
                    {"compositionHwnd", reinterpret_cast<std::uintptr_t>(windows.composition)}}))
                { heartbeatAt = renderEnd; heartbeatWritten = true; }
            }
            if (!measurementStarted)
            {
                const bool qualified = warmupElapsed >= options.warmup && seconds(stableSince, renderEnd) >= 20
                    && current.shared.trustState == Backend::CompositionDiagnostics::TrustState::Trusted
                    && runtime > 0 && current.uploads > 0 && current.ordinary.ready
                    && recentOrdinaryActivity;
                if (qualified)
                {
                    measurementStarted = true; measureAt = renderEnd; runtimeStart = runtime;
                    measurementWindow["start"] = measurementBoundary(origin, renderEnd, renderEndCounter);
                    measurementStart = current; activityWindowStart = current; activityWindowAt = renderEnd;
                    output.event("warmup_qualified", elapsed(), snapshotJson(current));
                    output.event("measurement_start", elapsed(), {{"runtimeSeconds", runtime}, {"warmupElapsedSeconds", warmupElapsed},
                        {"boundary", measurementWindow["start"]}});
                    if (output.heartbeat({{"utc", utcNow()}, {"phase", "measurement"}, {"sequence", sequence},
                        {"elapsedSeconds", elapsed()}, {"runtimeSeconds", runtime}, {"lastUpdateSequence", sequence}, {"lastRenderSequence", sequence},
                        {"ordinaryHwnd", reinterpret_cast<std::uintptr_t>(windows.ordinary)}, {"compositionHwnd", reinterpret_cast<std::uintptr_t>(windows.composition)}}))
                    { heartbeatAt = renderEnd; heartbeatWritten = true; }
                }
                else require(warmupElapsed < options.warmupTimeout, "warmup qualification timeout");
            }
            else if (measuredElapsed >= options.measure)
            {
                measurementWindow["end"] = measurementBoundary(origin, renderEnd, renderEndCounter);
                completed = true;
            }
            previousRuntime = runtime;
            last = current;
            // Emit the timing field last so it includes row preparation,
            // validation, sparse events and heartbeat I/O. Its own final scalar,
            // newline and periodic CSV flush are necessarily outside this span;
            // those costs still appear in the next actual frame interval.
            csv << ',' << seconds(frameStart, Clock::now())*1000 << '\n';
            if (heartbeatWritten || seconds(frameFlushAt, Clock::now()) >= 1.0)
            { csv.flush(); frameFlushAt = Clock::now(); }
            // Skip elapsed deadlines after an overrun; never spin a catch-up
            // burst or redefine the schedule as sleep-after-work.
            deadline += period;
            const Point now = Clock::now();
            if (deadline < now) deadline += period * ((now - deadline) / period + 1);
        }
        const auto ordinaryDrawDelta = last.ordinaryCounters.drawCount - measurementStart.ordinaryCounters.drawCount;
        const auto compositionDrawDelta = last.compositionCounters.drawCount - measurementStart.compositionCounters.drawCount;
        ordinaryActivity = ordinaryActivity && ordinaryDrawDelta > 0;
        compositionActivity = compositionActivity && compositionDrawDelta > 0 && last.composition.ready;
        require(ordinaryActivity && runtimeEnd > runtimeStart, "measurement lacks ordinary activity or Runtime progression");
        auto endFacts = snapshotJson(last); endFacts["boundary"] = measurementWindow["end"];
        output.event("measurement_end", elapsed(), std::move(endFacts));
    }
    catch (const std::exception& error)
    {
        failure = error.what();
        output.event("failure", elapsed(), {{"reason", failure}});
    }

    // Success and failure take the same ordered release path. Even a failed
    // explicit detach is followed by Engine shutdown before HWND destruction.
    output.event("shutdown_begin", elapsed());
    const bool compositionDetached = !compositionId.isValid() || engine.detachPresentationTarget(compositionId);
    output.event("composition_detached", elapsed(), {{"success", compositionDetached}});
    const bool ordinaryDetached = !ordinaryId.isValid() || engine.detachPresentationTarget(ordinaryId);
    output.event("ordinary_detached", elapsed(), {{"success", ordinaryDetached}});
    engine.shutdown();
    const bool engineReleased = !engine.isInitialized();
    output.event("engine_shutdown", elapsed(), {{"released", engineReleased}});
    const bool windowsDestroyed = windows.destroy();
    output.event("windows_destroyed", elapsed(), {{"success", windowsDestroyed}});
    cleanShutdown = compositionDetached && ordinaryDetached && engineReleased && windowsDestroyed;
    if (!cleanShutdown && failure.empty()) failure = "unclean shutdown";
    ordinaryActivity = completed && measurementStarted && ordinaryActivity;
    compositionActivity = completed && measurementStarted && compositionActivity;
    const std::string result = !failure.empty() || !completed ? "FAIL" : compositionActivity ? "PASS" : "INCONCLUSIVE";
    const std::string reason = !failure.empty() ? failure : compositionActivity
        ? "intended workload and dual-target native activity qualified"
        : "resource/clock workload complete; sustained native DirectComposition activity insufficient";
    Json terminal{{"result", result}, {"reason", reason}, {"startUtc", startUtc}, {"endUtc", utcNow()},
        {"totalElapsedSeconds", elapsed()}, {"warmupElapsedSeconds", warmupElapsed}, {"measurementElapsedSeconds", measuredElapsed},
        {"measurementWindow", measurementWindow},
        {"warmupFrames", warmupFrames}, {"measurementFrames", measurementFrames}, {"runtimeStartSeconds", runtimeStart},
        {"runtimeEndSeconds", runtimeEnd}, {"runtimeDeltaSeconds", runtimeEnd-runtimeStart},
        {"measurementSuppliedDtSeconds", suppliedSum}, {"runtimeVsSuppliedDtSeconds", runtimeEnd-runtimeStart-suppliedSum},
        {"runtimeVsWallSeconds", runtimeEnd-runtimeStart-measuredElapsed}, {"maxRuntimeStallSeconds", maxRuntimeStall},
        {"ordinaryId", ordinaryValue}, {"compositionId", compositionValue}, {"initial", snapshotJson(initial)},
        {"measurementStart", snapshotJson(measurementStart)}, {"measurementEnd", snapshotJson(last)},
        {"resourceEpochChanges", resourceChanges}, {"presentationStateChanges", stateChanges},
        {"ordinaryCounterEpoch", ordinaryEpoch}, {"compositionCounterEpoch", compositionEpoch},
        {"ordinaryActivity", ordinaryActivity}, {"compositionActivity", compositionActivity}, {"cleanShutdown", cleanShutdown},
        {"compositionCoverage", compositionActivity ? "sustained draw/Present-attempt activity, not displayed FPS" : "insufficient: see state/counters"},
        {"observabilityLimits", "No raw native HRESULT distribution, successful/displayed-frame count, per-Present duration or scan-out latency"}};
    output.event("complete", elapsed(), {{"result", result}, {"cleanShutdown", cleanShutdown}});
    output.heartbeat({{"utc", utcNow()}, {"phase", "completed"}, {"sequence", sequence}, {"elapsedSeconds", elapsed()},
        {"runtimeSeconds", runtimeEnd}, {"lastUpdateSequence", sequence}, {"lastRenderSequence", sequence}});
    terminal["heartbeatReplacementDeferrals"] = output.heartbeatReplacementDeferrals;
    terminal["lastHeartbeatReplacementWin32Error"] = output.lastHeartbeatReplacementError;
    output.result(terminal);
    std::cout << result << ": " << reason << '\n';
    return result == "FAIL" ? 1 : 0;
}
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test") { testPacingObservation(); return 0; }
        return run(parse(argc, argv));
    }
    catch (const std::exception& error) { std::cerr << "Soak harness fatal: " << error.what() << '\n'; return 1; }
}
