// Test-only, exact-owned-window composite observation. Never captures the
// desktop, uses a screen DC, injects input, or changes the target window.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl.h>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void checked(HRESULT value, const char* message)
{
    if (FAILED(value)) throw std::runtime_error(std::string(message) + " HRESULT="
        + std::to_string(static_cast<unsigned long>(value)));
}
std::string utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}
std::string jsonString(const std::string& value)
{
    std::ostringstream result;
    result << '"';
    for (const unsigned char c : value)
    {
        switch (c)
        {
        case '\\': result << "\\\\"; break;
        case '"': result << "\\\""; break;
        case '\n': result << "\\n"; break;
        case '\r': result << "\\r"; break;
        case '\t': result << "\\t"; break;
        default:
            if (c < 0x20) result << "?";
            else result << c;
        }
    }
    result << '"';
    return result.str();
}
std::wstring windowClass(HWND window)
{
    wchar_t value[256]{};
    require(GetClassNameW(window, value, 256) > 0, "window-class-unavailable");
    return value;
}
struct NativeIdentity
{
    HWND root{}, preview{};
    DWORD pid{};
    UINT dpi{};
    RECT extended{}, client{};
    POINT clientOrigin{};
    std::wstring executable, rootClass;
};
NativeIdentity identify(HWND root, DWORD expectedPid, const std::filesystem::path& expectedExe)
{
    NativeIdentity value;
    value.root = root;
    require(root && IsWindow(root) && GetAncestor(root, GA_ROOT) == root, "not-an-exact-root-window");
    require(GetWindowThreadProcessId(root, &value.pid) && value.pid == expectedPid, "owned-root-pid-mismatch");
    value.rootClass = windowClass(root);
    require(value.rootClass.starts_with(L"Qt") && value.rootClass.find(L"QWindow") != std::wstring::npos,
        "target-is-not-a-Qt-application-root");
    require(expectedExe.is_absolute(), "expected-executable-must-be-absolute");
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, value.pid);
    require(process != nullptr, "owned-process-query-failed");
    wchar_t image[32768]{};
    DWORD length = static_cast<DWORD>(std::size(image));
    const bool readImage = QueryFullProcessImageNameW(process, 0, image, &length) != FALSE;
    CloseHandle(process);
    require(readImage, "owned-process-image-unavailable");
    value.executable.assign(image, length);
    const auto expected = std::filesystem::weakly_canonical(expectedExe).wstring();
    const auto actual = std::filesystem::weakly_canonical(value.executable).wstring();
    require(CompareStringOrdinal(expected.c_str(), -1, actual.c_str(), -1, TRUE) == CSTR_EQUAL,
        "owned-process-executable-mismatch");
    struct PreviewEnumeration { HWND root{}, preview{}; DWORD pid{}; int count{}; } enumeration{root, nullptr, value.pid};
    EnumChildWindows(root, [](HWND child, LPARAM data) -> BOOL {
        auto& found = *reinterpret_cast<PreviewEnumeration*>(data);
        wchar_t name[256]{};
        GetClassNameW(child, name, 256);
        if (std::wstring_view(name) == L"DSE.Production.NativePreview")
        {
            DWORD pid{};
            GetWindowThreadProcessId(child, &pid);
            if (pid == found.pid && GetParent(child) == found.root)
            {
                ++found.count;
                found.preview = child;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&enumeration));
    require(enumeration.count == 1, "target-must-own-exactly-one-direct-production-preview");
    value.preview = enumeration.preview;
    require(IsWindowVisible(root) && !IsIconic(root), "capture-target-not-currently-visible");
    checked(DwmGetWindowAttribute(root, DWMWA_EXTENDED_FRAME_BOUNDS, &value.extended, sizeof(value.extended)),
        "owned-window-frame-bounds");
    require(GetClientRect(root, &value.client) && ClientToScreen(root, &value.clientOrigin), "owned-window-client-bounds");
    value.dpi = GetDpiForWindow(root);
    require(value.dpi != 0 && value.client.right > 0 && value.client.bottom > 0, "owned-window-client-invalid");
    return value;
}
bool sameGeometry(const NativeIdentity& initial)
{
    DWORD pid{};
    RECT bounds{}, client{};
    POINT origin{};
    return IsWindow(initial.root) && IsWindow(initial.preview)
        && GetWindowThreadProcessId(initial.root, &pid) && pid == initial.pid
        && GetParent(initial.preview) == initial.root && GetDpiForWindow(initial.root) == initial.dpi
        && SUCCEEDED(DwmGetWindowAttribute(initial.root, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))
        && EqualRect(&bounds, &initial.extended) && GetClientRect(initial.root, &client)
        && EqualRect(&client, &initial.client) && ClientToScreen(initial.root, &origin)
        && origin.x == initial.clientOrigin.x && origin.y == initial.clientOrigin.y;
}
struct CapturedSample
{
    POINT client{}, capture{};
    unsigned red{}, green{}, blue{}, alpha{};
};
struct Observation
{
    std::mutex mutex;
    std::condition_variable changed;
    bool stopped = false;
    unsigned frames{}, width{}, height{};
    std::string error;
    std::vector<CapturedSample> samples;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
};
std::string capture(const NativeIdentity& identity, const std::vector<POINT>& points)
{
    using namespace winrt::Windows::Graphics::Capture;
    using namespace winrt::Windows::Graphics::DirectX;
    using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
    require(GraphicsCaptureSession::IsSupported(), "window-graphics-capture-unavailable");
    auto state = std::make_shared<Observation>();
    checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &state->device, nullptr, &state->context), "capture-d3d-device");
    ComPtr<IDXGIDevice> dxgi;
    checked(state->device.As(&dxgi), "capture-dxgi-device");
    ComPtr<IInspectable> inspectable;
    checked(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), &inspectable), "capture-winrt-device");
    IDirect3DDevice winDevice{nullptr};
    checked(inspectable->QueryInterface(winrt::guid_of<IDirect3DDevice>(), winrt::put_abi(winDevice)), "capture-device-interface");
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    checked(interop->CreateForWindow(identity.root, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)),
        "capture-exact-owned-window");
    auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(winDevice,
        DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());
    auto session = pool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(false);
    const std::weak_ptr<Observation> weak = state;
    const int offsetX = identity.clientOrigin.x - identity.extended.left;
    const int offsetY = identity.clientOrigin.y - identity.extended.top;
    const auto frameToken = pool.FrameArrived([weak, identity, points, offsetX, offsetY](auto const& sender, auto const&) noexcept {
        const auto current = weak.lock();
        if (!current) return;
        // This is the sole user of this helper's immediate context. The lock
        // also protects cleanup/error/result transfer; no target device used.
        std::lock_guard lock(current->mutex);
        if (current->stopped || current->frames >= 3) return;
        try
        {
            require(sameGeometry(identity), "owned-window-geometry-changed-during-capture");
            auto frame = sender.TryGetNextFrame();
            if (!frame) return;
            const auto size = frame.ContentSize();
            require(size.Width == identity.extended.right - identity.extended.left
                && size.Height == identity.extended.bottom - identity.extended.top,
                "capture-frame-does-not-match-current-window-bounds");
            const auto access = frame.Surface().template as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> texture;
            checked(access->GetInterface(IID_PPV_ARGS(&texture)), "capture-frame-texture");
            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            description.Usage = D3D11_USAGE_STAGING;
            description.BindFlags = 0;
            description.MiscFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            checked(current->device->CreateTexture2D(&description, nullptr, &staging), "capture-readback-texture");
            current->context->CopyResource(staging.Get(), texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            checked(current->context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "capture-readback-map");
            std::vector<CapturedSample> samples;
            for (const auto point : points)
            {
                const POINT sample{point.x + offsetX, point.y + offsetY};
                if (sample.x < 0 || sample.y < 0 || sample.x >= size.Width || sample.y >= size.Height)
                {
                    current->context->Unmap(staging.Get(), 0);
                    throw std::runtime_error("capture-sample-outside-current-window");
                }
                const auto pixel = static_cast<const unsigned char*>(mapped.pData)
                    + sample.y * mapped.RowPitch + sample.x * 4;
                samples.push_back({point, sample, pixel[2], pixel[1], pixel[0], pixel[3]});
            }
            current->context->Unmap(staging.Get(), 0);
            current->samples = std::move(samples);
            current->width = static_cast<unsigned>(size.Width);
            current->height = static_cast<unsigned>(size.Height);
            ++current->frames;
        }
        catch (const winrt::hresult_error& error) { current->error = winrt::to_string(error.message()); }
        catch (const std::exception& error) { current->error = error.what(); }
        catch (...) { current->error = "unhandled-window-capture-failure"; }
        current->changed.notify_all();
    });
    const auto closedToken = item.Closed([weak](auto const&, auto const&) noexcept {
        if (const auto current = weak.lock())
        {
            std::lock_guard lock(current->mutex);
            if (!current->stopped) current->error = "owned-window-closed-during-capture";
            current->changed.notify_all();
        }
    });
    session.StartCapture();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline)
    {
        {
            std::unique_lock lock(state->mutex);
            if (state->frames >= 3 || !state->error.empty()) break;
            state->changed.wait_for(lock, std::chrono::milliseconds(20));
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    {
        std::lock_guard lock(state->mutex);
        state->stopped = true;
    }
    pool.FrameArrived(frameToken);
    item.Closed(closedToken);
    session.Close();
    pool.Close();
    std::lock_guard lock(state->mutex);
    require(state->error.empty(), state->error.c_str());
    require(state->frames >= 3, "fewer-than-three-window-capture-frames-before-deadline");
    require(sameGeometry(identity), "owned-window-geometry-changed-before-result");
    std::ostringstream json;
    json << "{\"ok\":true,\"method\":\"Windows.Graphics.Capture.CreateForWindow\",\"desktopCapture\":false,"
        << "\"rootHwnd\":\"" << reinterpret_cast<uintptr_t>(identity.root) << "\",\"expectedPid\":" << identity.pid
        << ",\"executable\":" << jsonString(utf8(identity.executable)) << ",\"rootClass\":" << jsonString(utf8(identity.rootClass))
        << ",\"previewHwnd\":\"" << reinterpret_cast<uintptr_t>(identity.preview) << "\",\"dpi\":" << identity.dpi
        << ",\"frameCount\":" << state->frames << ",\"width\":" << state->width << ",\"height\":" << state->height
        << ",\"clientOffset\":{\"x\":" << offsetX << ",\"y\":" << offsetY << "},\"clientSize\":{\"width\":"
        << identity.client.right << ",\"height\":" << identity.client.bottom << "},\"samples\":[";
    bool first = true;
    for (const auto& sample : state->samples)
    {
        if (!first) json << ',';
        first = false;
        json << "{\"client\":{\"x\":" << sample.client.x << ",\"y\":" << sample.client.y
            << "},\"capture\":{\"x\":" << sample.capture.x << ",\"y\":" << sample.capture.y << "},\"rgb\":["
            << sample.red << ',' << sample.green << ',' << sample.blue << "],\"alpha\":" << sample.alpha << '}';
    }
    json << "]}";
    return json.str();
}
}

int wmain(int argc, wchar_t** argv)
{
    std::filesystem::path output;
    bool outputValidated = false;
    try
    {
        require(argc >= 7 && (argc - 5) % 2 == 0,
            "usage: SessionCompositionCapture rootHwnd expectedPid absoluteExe absoluteOutputJson clientX clientY [...]");
        output = argv[4];
        require(output.is_absolute() && std::filesystem::is_directory(output.parent_path()), "output-parent-must-exist-and-be-absolute");
        require(!std::filesystem::exists(output), "refuse-overwriting-existing-capture-evidence");
        outputValidated = true;
        const auto hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(std::stoull(argv[1])));
        const auto pidValue = std::stoull(argv[2]);
        require(pidValue > 0 && pidValue <= MAXDWORD, "expected-pid-invalid");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        const auto identity = identify(hwnd, static_cast<DWORD>(pidValue), argv[3]);
        std::vector<POINT> points;
        for (int index = 5; index + 1 < argc; index += 2)
        {
            size_t xEnd{}, yEnd{};
            const auto x = std::stol(argv[index], &xEnd);
            const auto y = std::stol(argv[index + 1], &yEnd);
            require(xEnd == std::wstring_view(argv[index]).size() && yEnd == std::wstring_view(argv[index + 1]).size()
                && x >= 0 && y >= 0 && x < identity.client.right && y < identity.client.bottom,
                "sample-must-be-an-integer-inside-current-physical-client-bounds");
            points.push_back({x, y});
        }
        const auto result = capture(identity, points);
        std::ofstream stream(output, std::ios::binary | std::ios::out);
        require(stream.good(), "capture-evidence-open-failed");
        stream << result << '\n';
        require(stream.good(), "capture-evidence-write-failed");
        std::cout << result << '\n';
        winrt::uninit_apartment();
        return 0;
    }
    catch (const winrt::hresult_error& error)
    {
        const auto json = std::string("{\"ok\":false,\"error\":") + jsonString(winrt::to_string(error.message())) + "}";
        if (outputValidated && !std::filesystem::exists(output)) { std::ofstream stream(output); stream << json << '\n'; }
        std::cerr << json << '\n';
    }
    catch (const std::exception& error)
    {
        const auto json = std::string("{\"ok\":false,\"error\":") + jsonString(error.what()) + "}";
        if (outputValidated && !std::filesystem::exists(output)) { std::ofstream stream(output); stream << json << '\n'; }
        std::cerr << json << '\n';
    }
    return 1;
}
