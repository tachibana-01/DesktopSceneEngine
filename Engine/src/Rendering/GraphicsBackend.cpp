#include "GraphicsBackend.h"
#include "PresentationViewport.h"

#include "../Platform/Windows/NativeWindowClientSize.h"

#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace
{

using Microsoft::WRL::ComPtr;
using DesktopSceneEngine::Rendering::Detail::PresentationTargetState;
using DesktopSceneEngine::Rendering::Detail::CompositionFailurePoint;
using DesktopSceneEngine::Rendering::Detail::GraphicsOperationResult;
using DesktopSceneEngine::PresentationTransport;
using DesktopSceneEngine::CanvasOutsideMode;

enum class CompositionTrustState { Trusted, UntrustedPending, UntrustedSpent };

struct DeviceResources
{
    ComPtr<ID3D11Device> device{};
    ComPtr<ID3D11DeviceContext> context{};
    ComPtr<IDXGIFactory> factory{};
    // Lazy capability of this shared D3D11/DXGI generation, not of a target.
    ComPtr<IDCompositionDevice> compositionDevice{};
    CompositionTrustState compositionTrustState = CompositionTrustState::Trusted;
    CompositionFailurePoint compositionFailureForTesting = CompositionFailurePoint::None;
    HRESULT compositionFailureResultForTesting = E_FAIL;
    CompositionFailurePoint compositionRollbackFailureForTesting = CompositionFailurePoint::None;
    HRESULT compositionRollbackFailureResultForTesting = E_FAIL;
    std::uint64_t compositionCapabilityCreationCountForTesting = 0U;
    std::uint64_t compositionCommitCountForTesting = 0U;
    std::uint64_t compositionWaitCountForTesting = 0U;
    HRESULT lastCompositionDisconnectResultForTesting = S_OK;
    ComPtr<ID3D11VertexShader> vertexShader{};
    ComPtr<ID3D11PixelShader> pixelShader{};
    ComPtr<ID3D11SamplerState> samplerState{};
    ComPtr<ID3D11Buffer> presentationConstantBuffer{};
    std::uint64_t generation = 0U;
    std::uint64_t updateSubresourceCalls = 0;
};

bool isDeviceLost(const DeviceResources& deviceResources) noexcept;

GraphicsOperationResult classifyDeviceOperation(
    const DeviceResources& resources, HRESULT result) noexcept
{
    return DesktopSceneEngine::Rendering::Detail::classifyGraphicsOperationResult(
        static_cast<std::int32_t>(result),
        static_cast<std::int32_t>(resources.device
            ? resources.device->GetDeviceRemovedReason() : S_OK));
}

struct CurrentMediaRealization
{
    ComPtr<ID3D11Texture2D> texture{};
    ComPtr<ID3D11ShaderResourceView> shaderResourceView{};
    int width = 0;
    int height = 0;
    int channels = 0;
    std::uint64_t uploadCountForTesting = 0U;
};

struct HwndSwapChainResources {};

enum class CompositionTreeState { Unwired, ContentAccepted, RootAccepted, Submitted };

struct CompositionSwapChainResources
{
    ComPtr<IDCompositionTarget> target{};
    ComPtr<IDCompositionVisual> visual{};
    CompositionTreeState treeState = CompositionTreeState::Unwired;
};

struct TargetDescriptor
{
    DesktopSceneEngine::PresentationTargetId id{};
    void* nativeHandle = nullptr;
    PresentationTransport transport = PresentationTransport::HwndSwapChain;
    int bottomInsetPixels = 0;
};

bool hasOtherLogicalCompositionTarget(
    const std::vector<TargetDescriptor>& descriptors,
    DesktopSceneEngine::PresentationTargetId excludedId) noexcept
{
    return std::any_of(descriptors.begin(), descriptors.end(),
        [excludedId](const TargetDescriptor& descriptor)
        {
            return descriptor.id != excludedId
                && descriptor.transport == PresentationTransport::DirectCompositionSwapChain;
        });
}

struct TargetRealization
{
    DesktopSceneEngine::PresentationTargetId id{};
    ComPtr<IDXGISwapChain> swapChain{};
    ComPtr<ID3D11RenderTargetView> renderTargetView{};
    std::variant<HwndSwapChainResources, CompositionSwapChainResources> transport{};
    int observedClientWidth = 0;
    int observedClientHeight = 0;
    int realizedClientWidth = 0;
    int realizedClientHeight = 0;
    std::uint64_t generation = 0U;
    std::uint64_t successfulResizeCountForTesting = 0U;
    std::uint64_t drawCountForTesting = 0U;
    std::uint64_t presentAttemptCountForTesting = 0U;
    std::uint64_t lastPresentOrderForTesting = 0U;
    UINT lastPresentSyncIntervalForTesting = 0U;
    UINT lastPresentFlagsForTesting = 0U;
    std::int32_t forcedPresentResultForTesting = 0;
    std::int32_t forcedDeviceRemovedReasonForTesting = 0;
    bool failNextRenderTargetViewCreationForTesting = false;
    bool hasForcedPresentResultForTesting = false;
    bool captureNextPixelForTesting = false;
    int capturePixelXForTesting = -1;
    int capturePixelYForTesting = -1;
    bool capturedPixelValidForTesting = false;
    std::uint32_t capturedPixelRgbaForTesting = 0U;
    PresentationTargetState state = PresentationTargetState::Detached;
    // One authorization on an already-published logical slot, not on its
    // current swap chain or render-target view.
    bool pendingTargetLocalRecovery = false;
};

static_assert(std::is_nothrow_move_constructible_v<TargetDescriptor>);
static_assert(std::is_nothrow_move_assignable_v<TargetDescriptor>);
static_assert(std::is_nothrow_move_constructible_v<TargetRealization>);
static_assert(std::is_nothrow_move_assignable_v<TargetRealization>);

struct GraphicsGeneration
{
    DeviceResources deviceResources{};
    CurrentMediaRealization currentMedia{};
    std::vector<TargetRealization> targets{};
};

bool isTargetEffectivelyReady(
    const DeviceResources& resources, const TargetRealization& target) noexcept
{
    return target.state == PresentationTargetState::Ready
        && (std::holds_alternative<HwndSwapChainResources>(target.transport)
            || resources.compositionTrustState == CompositionTrustState::Trusted);
}

void enterCompositionContainment(DeviceResources& resources) noexcept
{
    if (resources.compositionTrustState == CompositionTrustState::Trusted)
        resources.compositionTrustState = CompositionTrustState::UntrustedPending;
}

// A required disconnect failure affects the device-wide DComp transaction
// domain regardless of whether the endpoint being released is still a HWND.
[[nodiscard]] bool applyRequiredCompositionDisconnectOutcome(
    DeviceResources& resources, HRESULT result,
    bool hasContinuingLogicalCompositionTarget) noexcept
{
    if (classifyDeviceOperation(resources, result) == GraphicsOperationResult::DeviceLost)
        return true;
    if (FAILED(result))
    {
        if (hasContinuingLogicalCompositionTarget)
            enterCompositionContainment(resources);
        else
        {
            resources.compositionDevice.Reset();
            resources.compositionTrustState = CompositionTrustState::Trusted;
        }
    }
    return false;
}

bool validTransport(PresentationTransport transport) noexcept
{
    return transport == PresentationTransport::HwndSwapChain
        || transport == PresentationTransport::DirectCompositionSwapChain;
}

template<class Operation>
HRESULT compositionOperation(DeviceResources& resources,
    CompositionFailurePoint point, const char* name, Operation operation) noexcept
{
    const bool forceFailure = resources.compositionFailureForTesting == point;
    const bool forceRollbackFailure = resources.compositionRollbackFailureForTesting == point;
    if (forceFailure)
        resources.compositionFailureForTesting = CompositionFailurePoint::None;
    if (forceRollbackFailure)
        resources.compositionRollbackFailureForTesting = CompositionFailurePoint::None;
    const HRESULT result = forceFailure ? resources.compositionFailureResultForTesting
        : forceRollbackFailure ? resources.compositionRollbackFailureResultForTesting
        : operation();
    if (FAILED(result))
    {
        std::fprintf(stderr, "[GraphicsBackend] %s failed: HRESULT=0x%08lX\n",
            name, static_cast<unsigned long>(result));
        std::fflush(stderr);
    }
    return result;
}

HRESULT commitComposition(DeviceResources& resources, IDCompositionDevice* compositionDevice,
    CompositionFailurePoint point) noexcept
{
    ++resources.compositionCommitCountForTesting;
    return compositionOperation(resources, point, "DComp Commit", [&]()
        { return compositionDevice->Commit(); });
}

HRESULT commitComposition(DeviceResources& resources,
    CompositionFailurePoint point) noexcept
{
    return commitComposition(resources, resources.compositionDevice.Get(), point);
}

// Disconnect actual partially-created objects as well as complete trees. Object
// destruction frees the native HWND layer even when disconnect submission fails.
HRESULT releasePresentationDrawableResources(DeviceResources& resources,
    TargetRealization& target, bool allowCompositionDisconnect = true) noexcept
{
    HRESULT requiredTransactionResult = S_OK;
    target.state = PresentationTargetState::Failed; // Exclude before disconnect.
    if (resources.context)
        resources.context->OMSetRenderTargets(0, nullptr, nullptr);
    if (auto* composition = std::get_if<CompositionSwapChainResources>(&target.transport))
    {
        if (allowCompositionDisconnect
            && resources.compositionTrustState == CompositionTrustState::Trusted
            && resources.compositionDevice
            && resources.device && SUCCEEDED(resources.device->GetDeviceRemovedReason())
            && (composition->target || composition->visual))
        {
            HRESULT result = S_OK;
            HRESULT deviceLostResult = S_OK;
            const auto retainFailure = [&result, &deviceLostResult, &resources](HRESULT current)
            {
                if (FAILED(current)
                    && classifyDeviceOperation(resources, current)
                        == GraphicsOperationResult::DeviceLost)
                    deviceLostResult = current;
                if (FAILED(current) && SUCCEEDED(result))
                    result = current;
            };
            if (composition->target)
                retainFailure(compositionOperation(resources,
                    CompositionFailurePoint::DisconnectSetRoot, "DComp disconnect SetRoot", [&]()
                    { return composition->target->SetRoot(nullptr); }));
            if (composition->visual)
                retainFailure(compositionOperation(resources,
                    CompositionFailurePoint::DisconnectSetContent, "DComp disconnect SetContent", [&]()
                    { return composition->visual->SetContent(nullptr); }));
            const HRESULT committed = commitComposition(resources,
                CompositionFailurePoint::DisconnectCommit);
            retainFailure(committed);
            if (SUCCEEDED(committed))
            {
                ++resources.compositionWaitCountForTesting;
                const HRESULT waited = compositionOperation(resources,
                    CompositionFailurePoint::DisconnectWait, "DComp disconnect wait", [&]()
                    { return resources.compositionDevice->WaitForCommitCompletion(); });
                retainFailure(waited);
            }
            resources.lastCompositionDisconnectResultForTesting = result;
            if (FAILED(result))
                std::fprintf(stderr, "[GraphicsBackend] composition binding released after disconnect failure: 0x%08lX\n",
                    static_cast<unsigned long>(result));
            // A successful Commit/Wait cannot erase an earlier required setter
            // failure; any DeviceLost-class operation takes precedence.
            requiredTransactionResult = FAILED(deviceLostResult) ? deviceLostResult : result;
        }
        target.renderTargetView.Reset();
        composition->visual.Reset();
        composition->target.Reset();
        composition->treeState = CompositionTreeState::Unwired;
    }
    target.renderTargetView.Reset();
    target.swapChain.Reset();
    return requiredTransactionResult;
}

constexpr char FullscreenVertexShader[] = R"(
cbuffer PresentationConstants : register(b0)
{
    float4 sourceUv;
    float4 destination;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VertexOutput main(uint vertexId : SV_VertexID)
{
    // The default D3D11 rasterizer culls back-facing triangles, so this strip
    // remains clockwise after the Canvas-to-clip-space Y inversion below.
    float2 positions[4] = {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 1.0f)
    };

    float2 uvs[4] = {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 1.0f)
    };

    VertexOutput output;
    const float2 canvasPosition = destination.xy + positions[vertexId] * destination.zw;
    output.position = float4(
        canvasPosition.x * 2.0f - 1.0f,
        1.0f - canvasPosition.y * 2.0f,
        0.0f,
        1.0f);
    output.uv = sourceUv.xy + uvs[vertexId] * sourceUv.zw;
    return output;
}
)";

constexpr char ImagePixelShader[] = R"(
Texture2D imageTexture : register(t0);
SamplerState imageSampler : register(s0);

struct PixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PixelInput input) : SV_TARGET
{
    // Final presentation is opaque; source media remains straight RGBA.
    const float4 sampled = imageTexture.Sample(imageSampler, input.uv);
    return float4(sampled.rgb, 1.0f);
}
)";

bool createShaderResources(DeviceResources& resources)
{
    ComPtr<ID3DBlob> vertexByteCode{};
    ComPtr<ID3DBlob> pixelByteCode{};
    const UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;

    if (FAILED(D3DCompile(
        FullscreenVertexShader,
        sizeof(FullscreenVertexShader) - 1,
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        compileFlags,
        0,
        &vertexByteCode,
        nullptr))
        || FAILED(D3DCompile(
            ImagePixelShader,
            sizeof(ImagePixelShader) - 1,
            nullptr,
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            compileFlags,
            0,
            &pixelByteCode,
            nullptr)))
    {
        return false;
    }

    if (FAILED(resources.device->CreateVertexShader(
        vertexByteCode->GetBufferPointer(),
        vertexByteCode->GetBufferSize(),
        nullptr,
        &resources.vertexShader))
        || FAILED(resources.device->CreatePixelShader(
            pixelByteCode->GetBufferPointer(),
            pixelByteCode->GetBufferSize(),
            nullptr,
            &resources.pixelShader)))
    {
        return false;
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDescription.MinLOD = 0.0F;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;

    if (FAILED(resources.device->CreateSamplerState(
        &samplerDescription,
        &resources.samplerState)))
    {
        return false;
    }

    D3D11_BUFFER_DESC constantBufferDescription{};
    constantBufferDescription.ByteWidth = 32U;
    constantBufferDescription.Usage = D3D11_USAGE_DEFAULT;
    constantBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    return SUCCEEDED(resources.device->CreateBuffer(
        &constantBufferDescription,
        nullptr,
        &resources.presentationConstantBuffer));
}

HRESULT createRenderTargetView(
    DeviceResources& deviceResources,
    IDXGISwapChain* swapChain,
    ComPtr<ID3D11RenderTargetView>& renderTargetView)
{
    ComPtr<ID3D11Texture2D> backBuffer{};
    if (swapChain == nullptr)
        return E_POINTER;
    const HRESULT result = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(result))
    {
        return result;
    }

    return deviceResources.device->CreateRenderTargetView(
        backBuffer.Get(),
        nullptr,
        &renderTargetView);
}

int viewportHeight(
    const TargetDescriptor& descriptor,
    const TargetRealization& target) noexcept
{
    return (std::max)(
        target.realizedClientHeight - descriptor.bottomInsetPixels,
        0);
}

bool hasDrawableViewport(
    const TargetDescriptor& descriptor,
    const TargetRealization& target) noexcept
{
    return target.realizedClientWidth > 0 && viewportHeight(descriptor, target) > 0;
}

void setViewport(
    DeviceResources& deviceResources,
    const DesktopSceneEngine::Rendering::PresentationViewport& derived)
{
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = derived.x;
    viewport.TopLeftY = derived.y;
    viewport.Width = derived.width;
    viewport.Height = derived.height;
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    deviceResources.context->RSSetViewports(1, &viewport);
}

bool hasValidRgbaBuffer(
    int width,
    int height,
    int channels,
    std::size_t rowStride,
    std::size_t byteSize)
{
    if (width <= 0 || height <= 0 || channels != 4)
    {
        return false;
    }

    const std::size_t minimumRowStride =
        static_cast<std::size_t>(width) * 4U;
    if (rowStride < minimumRowStride
        || rowStride > (std::numeric_limits<UINT>::max)())
    {
        return false;
    }

    const std::size_t rowCount = static_cast<std::size_t>(height);
    if (rowStride > (std::numeric_limits<std::size_t>::max)() / rowCount)
    {
        return false;
    }

    return byteSize >= rowStride * rowCount;
}

DesktopSceneEngine::Rendering::Detail::MediaUpdateResult createMediaRealization(
    DeviceResources& resources,
    CurrentMediaRealization& media,
    const DesktopSceneEngine::Rendering::Detail::MediaView& view)
{
    using DesktopSceneEngine::Rendering::Detail::GraphicsOperationResult;
    using DesktopSceneEngine::Rendering::Detail::MediaUpdateResult;
    if (!resources.device || view.data == nullptr
        || !hasValidRgbaBuffer(view.width, view.height, view.channels,
            view.rowStride, view.byteSize))
        return MediaUpdateResult::ResourceFailure;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(view.width);
    description.Height = static_cast<UINT>(view.height);
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = view.data;
    initialData.SysMemPitch = static_cast<UINT>(view.rowStride);

    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> shaderResourceView;
    HRESULT result = resources.device->CreateTexture2D(
        &description, &initialData, &texture);
    if (SUCCEEDED(result))
        result = resources.device->CreateShaderResourceView(
            texture.Get(), nullptr, &shaderResourceView);
    const HRESULT removedReason = resources.device->GetDeviceRemovedReason();
    const auto classification = DesktopSceneEngine::Rendering::Detail::
        classifyGraphicsOperationResult(
            static_cast<std::int32_t>(result),
            static_cast<std::int32_t>(removedReason));
    if (classification == GraphicsOperationResult::DeviceLost)
        return MediaUpdateResult::DeviceLost;
    if (classification == GraphicsOperationResult::ResourceFailure)
        return MediaUpdateResult::ResourceFailure;

    media.texture = std::move(texture);
    media.shaderResourceView = std::move(shaderResourceView);
    media.width = view.width;
    media.height = view.height;
    media.channels = view.channels;
    ++media.uploadCountForTesting;
    return MediaUpdateResult::Updated;
}

bool hasValidPresentation(const DesktopSceneEngine::Rendering::RenderPresentation& presentation)
{
    const auto finite = [](float value)
    {
        return std::isfinite(value);
    };
    const auto& source = presentation.sourceUv;
    const auto& destination = presentation.destination;
    return finite(source.x) && finite(source.y)
        && finite(source.width) && finite(source.height)
        && source.x >= 0.0F && source.y >= 0.0F
        && source.width > 0.0F && source.height > 0.0F
        && source.x + source.width <= 1.0F
        && source.y + source.height <= 1.0F
        && finite(destination.x) && finite(destination.y)
        && finite(destination.width) && finite(destination.height)
        && destination.width > 0.0F && destination.height > 0.0F;
}

struct PresentationConstants
{
    float sourceUv[4]{};
    float destination[4]{};
};

bool createDeviceResources(DeviceResources& resources)
{
    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL selectedFeatureLevel{};
    if (FAILED(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &resources.device,
            &selectedFeatureLevel,
            &resources.context)))
    {
        return false;
    }

    (void)selectedFeatureLevel;

    ComPtr<IDXGIDevice> dxgiDevice{};
    ComPtr<IDXGIAdapter> adapter{};
    if (FAILED(resources.device.As(&dxgiDevice))
        || FAILED(dxgiDevice->GetAdapter(&adapter))
        || FAILED(adapter->GetParent(IID_PPV_ARGS(&resources.factory)))
        || !createShaderResources(resources))
    {
        return false;
    }

    ++resources.generation;
    return true;
}

HRESULT createHwndSwapChainResources(
    DeviceResources& deviceResources,
    const TargetDescriptor& descriptor,
    TargetRealization& target,
    const DesktopSceneEngine::Platform::Windows::NativeClientSize& clientSize)
{
    const HWND window = reinterpret_cast<HWND>(descriptor.nativeHandle);
    if (!deviceResources.device || !deviceResources.factory
        || window == nullptr || clientSize.width <= 0 || clientSize.height <= 0)
    {
        return E_INVALIDARG;
    }

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 1;
    description.BufferDesc.Width = static_cast<UINT>(clientSize.width);
    description.BufferDesc.Height = static_cast<UINT>(clientSize.height);
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;

    ComPtr<IDXGISwapChain> swapChain{};
    ComPtr<ID3D11RenderTargetView> renderTargetView{};
    HRESULT result = deviceResources.factory->CreateSwapChain(
        deviceResources.device.Get(), &description, &swapChain);
    if (FAILED(result))
        return result;
    result = createRenderTargetView(deviceResources, swapChain.Get(), renderTargetView);
    if (FAILED(result))
        return result;

    target.swapChain = std::move(swapChain);
    target.renderTargetView = std::move(renderTargetView);
    target.realizedClientWidth = clientSize.width;
    target.realizedClientHeight = clientSize.height;
    ++target.generation;
    target.state = hasDrawableViewport(descriptor, target)
        ? PresentationTargetState::Ready
        : PresentationTargetState::Suspended;
    return S_OK;
}

HRESULT createCompositionDevice(DeviceResources& resources,
    ComPtr<IDCompositionDevice>& compositionDevice)
{
    ComPtr<IDXGIDevice> dxgiDevice;
    const HRESULT deviceResult = resources.device.As(&dxgiDevice);
    if (FAILED(deviceResult))
        return deviceResult;
    const HRESULT result = compositionOperation(resources,
        CompositionFailurePoint::CapabilityCreation, "DComp capability creation", [&]()
        {
            return DCompositionCreateDevice(dxgiDevice.Get(),
                IID_PPV_ARGS(&compositionDevice));
        });
    if (FAILED(result))
        return result;
    ++resources.compositionCapabilityCreationCountForTesting;
    return S_OK;
}

HRESULT ensureCompositionDevice(DeviceResources& resources)
{
    if (resources.compositionTrustState != CompositionTrustState::Trusted)
        return E_FAIL;
    if (resources.compositionDevice)
        return S_OK;
    return createCompositionDevice(resources, resources.compositionDevice);
}

// Stage A is D3D/DXGI-only. It cannot mutate a composition transaction domain.
HRESULT prepareCompositionSwapChainResources(DeviceResources& resources,
    IDXGIFactory2* factory,
    TargetRealization& target,
    const DesktopSceneEngine::Platform::Windows::NativeClientSize& clientSize)
{
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast<UINT>(clientSize.width);
    description.Height = static_cast<UINT>(clientSize.height);
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    ComPtr<IDXGISwapChain1> chain;
    HRESULT result = compositionOperation(resources, CompositionFailurePoint::SwapChainCreation,
        "composition swap-chain creation", [&]()
        { return factory->CreateSwapChainForComposition(resources.device.Get(),
            &description, nullptr, &chain); });
    if (FAILED(result))
        return result;
    target.swapChain = std::move(chain);
    target.realizedClientWidth = clientSize.width;
    target.realizedClientHeight = clientSize.height;
    result = compositionOperation(resources, CompositionFailurePoint::RenderTargetViewCreation,
        "composition RTV creation", [&]()
        { return createRenderTargetView(resources, target.swapChain.Get(),
            target.renderTargetView); });
    if (FAILED(result))
        return result;
    return S_OK;
}

// Stage B stops before Commit so recovery can submit one coordinated batch.
HRESULT bindCompositionTarget(DeviceResources& resources,
    IDCompositionDevice* compositionDevice,
    const TargetDescriptor& descriptor, TargetRealization& target)
{
    auto& composition = std::get<CompositionSwapChainResources>(target.transport);
    HRESULT result = compositionOperation(resources, CompositionFailurePoint::TargetCreation,
        "DComp HWND target creation", [&]()
        { return compositionDevice->CreateTargetForHwnd(
            reinterpret_cast<HWND>(descriptor.nativeHandle), TRUE, &composition.target); });
    if (FAILED(result))
        return result;
    result = compositionOperation(resources, CompositionFailurePoint::VisualCreation,
        "DComp visual creation", [&]()
        { return compositionDevice->CreateVisual(&composition.visual); });
    if (FAILED(result))
        return result;
    result = compositionOperation(resources, CompositionFailurePoint::SetContent,
        "DComp SetContent", [&]()
        { return composition.visual->SetContent(target.swapChain.Get()); });
    if (FAILED(result))
        return result;
    composition.treeState = CompositionTreeState::ContentAccepted;
    result = compositionOperation(resources, CompositionFailurePoint::SetRoot,
        "DComp SetRoot", [&]()
        { return composition.target->SetRoot(composition.visual.Get()); });
    if (FAILED(result))
        return result;
    composition.treeState = CompositionTreeState::RootAccepted;
    return S_OK;
}

HRESULT createCompositionSwapChainResources(DeviceResources& resources,
    const TargetDescriptor& descriptor,
    TargetRealization& target,
    const DesktopSceneEngine::Platform::Windows::NativeClientSize& clientSize)
{
    HRESULT result = ensureCompositionDevice(resources);
    if (FAILED(result))
        return result;
    ComPtr<IDXGIFactory2> factory;
    result = resources.factory.As(&factory);
    if (FAILED(result))
        return result;
    result = prepareCompositionSwapChainResources(resources, factory.Get(), target, clientSize);
    if (FAILED(result))
        return result;
    result = bindCompositionTarget(resources, resources.compositionDevice.Get(), descriptor, target);
    if (FAILED(result))
        return result;
    result = commitComposition(resources, CompositionFailurePoint::AttachCommit);
    if (FAILED(result))
        return result;
    std::get<CompositionSwapChainResources>(target.transport).treeState
        = CompositionTreeState::Submitted;
    ++target.generation;
    target.state = hasDrawableViewport(descriptor, target)
        ? PresentationTargetState::Ready : PresentationTargetState::Suspended;
    return S_OK;
}

struct CompositionRecoveryCandidate
{
    std::size_t slotIndex = 0U;
    TargetRealization target{};
};

static_assert(std::is_nothrow_move_constructible_v<CompositionRecoveryCandidate>);
static_assert(std::is_nothrow_move_assignable_v<CompositionRecoveryCandidate>);

// All recovery ownership is attempt-local. An early return releases targets
// before the fresh device, without submitting a transaction to either domain.
GraphicsOperationResult recoverCompositionDomain(GraphicsGeneration& generation,
    const std::vector<TargetDescriptor>& descriptors)
{
    auto& resources = generation.deviceResources;
    resources.compositionTrustState = CompositionTrustState::UntrustedSpent;
    ComPtr<IDCompositionDevice> freshDevice;
    std::vector<CompositionRecoveryCandidate> candidates;
    const auto retireOldDomain = [&]() noexcept
    {
        for (auto& target : generation.targets)
            if (std::holds_alternative<CompositionSwapChainResources>(target.transport))
            {
                (void)releasePresentationDrawableResources(resources, target, false);
                target.realizedClientWidth = 0;
                target.realizedClientHeight = 0;
            }
        resources.compositionDevice.Reset();
    };
    const auto candidateCapacity = static_cast<std::size_t>(std::count_if(
        descriptors.begin(), descriptors.end(), [](const TargetDescriptor& descriptor)
        { return descriptor.transport == PresentationTransport::DirectCompositionSwapChain; }));
    if (candidateCapacity > candidates.max_size())
    {
        retireOldDomain();
        return classifyDeviceOperation(resources, E_OUTOFMEMORY);
    }
    try
    {
        candidates.reserve(candidateCapacity);
    }
    catch (const std::bad_alloc&)
    {
        retireOldDomain();
        return classifyDeviceOperation(resources, E_OUTOFMEMORY);
    }
    retireOldDomain();
    const HRESULT created = createCompositionDevice(resources, freshDevice);
    if (FAILED(created) || isDeviceLost(resources))
        return classifyDeviceOperation(resources, created);

    // Observe the entire logical working set before target construction.
    for (std::size_t index = 0; index < descriptors.size(); ++index)
    {
        if (descriptors[index].transport != PresentationTransport::DirectCompositionSwapChain)
            continue;
        auto& target = generation.targets[index];
        const auto size = DesktopSceneEngine::Platform::Windows::queryNativeWindowClientSize(
            descriptors[index].nativeHandle);
        if (!size)
        {
            target.pendingTargetLocalRecovery = false;
            continue;
        }
        target.observedClientWidth = size->width;
        target.observedClientHeight = size->height;
        if (size->width == 0 || size->height == 0)
        {
            target.state = PresentationTargetState::Suspended;
            continue;
        }
        candidates.push_back({index, target});
    }
    ComPtr<IDXGIFactory2> factory;
    if (!candidates.empty())
    {
        const HRESULT queried = resources.factory.As(&factory);
        if (FAILED(queried) || isDeviceLost(resources))
            return classifyDeviceOperation(resources, queried);
    }
    for (auto& candidate : candidates)
    {
        const DesktopSceneEngine::Platform::Windows::NativeClientSize size{
            candidate.target.observedClientWidth, candidate.target.observedClientHeight};
        // Validation, native-size observation and the shared factory query are
        // not target reconstruction. Consume only at its first construction call.
        generation.targets[candidate.slotIndex].pendingTargetLocalRecovery = false;
        candidate.target.pendingTargetLocalRecovery = false;
        const HRESULT prepared = prepareCompositionSwapChainResources(
            resources, factory.Get(), candidate.target, size);
        const auto category = classifyDeviceOperation(resources, prepared);
        if (category == GraphicsOperationResult::DeviceLost)
            return category;
        if (FAILED(prepared))
            (void)releasePresentationDrawableResources(resources, candidate.target, false);
    }

    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
        [](const CompositionRecoveryCandidate& candidate)
        { return !candidate.target.swapChain || !candidate.target.renderTargetView; }), candidates.end());
    factory.Reset();
    bool stageBBegun = false; // Attempt-local control flow, never persistent trust.
    for (auto candidate = candidates.begin(); candidate != candidates.end();)
    {
        if (!IsWindow(reinterpret_cast<HWND>(descriptors[candidate->slotIndex].nativeHandle)))
        {
            if (stageBBegun)
                return classifyDeviceOperation(resources, E_HANDLE);
            generation.targets[candidate->slotIndex].pendingTargetLocalRecovery = false;
            (void)releasePresentationDrawableResources(resources, candidate->target, false);
            candidate = candidates.erase(candidate);
            continue;
        }
        stageBBegun = true; // Includes a failing very first CreateTargetForHwnd.
        const HRESULT bound = bindCompositionTarget(resources, freshDevice.Get(),
            descriptors[candidate->slotIndex], candidate->target);
        if (FAILED(bound) || isDeviceLost(resources))
            return classifyDeviceOperation(resources, bound);
        ++candidate;
    }
    if (!stageBBegun)
    {
        if (isDeviceLost(resources))
            return GraphicsOperationResult::DeviceLost;
        resources.compositionDevice = std::move(freshDevice);
        resources.compositionTrustState = CompositionTrustState::Trusted;
        return GraphicsOperationResult::Succeeded;
    }

    for (const auto& candidate : candidates)
        if (!IsWindow(reinterpret_cast<HWND>(descriptors[candidate.slotIndex].nativeHandle)))
            return classifyDeviceOperation(resources, E_HANDLE);
    const HRESULT committed = commitComposition(resources, freshDevice.Get(),
        CompositionFailurePoint::RecoveryCommit);
    if (FAILED(committed) || isDeviceLost(resources))
        return classifyDeviceOperation(resources, committed);
    ++resources.compositionWaitCountForTesting;
    const HRESULT waited = compositionOperation(resources,
        CompositionFailurePoint::RecoveryWait, "DComp recovery wait", [&]()
        { return freshDevice->WaitForCommitCompletion(); });
    if (FAILED(waited) || isDeviceLost(resources))
        return classifyDeviceOperation(resources, waited);

    for (const auto& candidate : candidates)
    {
        const auto& composition = std::get<CompositionSwapChainResources>(candidate.target.transport);
        if (!candidate.target.swapChain || !candidate.target.renderTargetView
            || !composition.target || !composition.visual
            || composition.treeState != CompositionTreeState::RootAccepted
            || candidate.target.id != descriptors[candidate.slotIndex].id
            || !IsWindow(reinterpret_cast<HWND>(descriptors[candidate.slotIndex].nativeHandle)))
            return classifyDeviceOperation(resources, E_FAIL);
    }
    if (isDeviceLost(resources))
        return GraphicsOperationResult::DeviceLost;

    // Publication is allocation/native-call/callback-free. All destination
    // native owners were emptied at retirement, and moves are nothrow.
    resources.compositionDevice = std::move(freshDevice);
    for (auto& candidate : candidates)
    {
        auto& target = generation.targets[candidate.slotIndex];
        target = std::move(candidate.target);
        std::get<CompositionSwapChainResources>(target.transport).treeState
            = CompositionTreeState::Submitted;
        ++target.generation;
        target.state = hasDrawableViewport(descriptors[candidate.slotIndex], target)
            ? PresentationTargetState::Ready : PresentationTargetState::Suspended;
    }
    resources.compositionTrustState = CompositionTrustState::Trusted; // LAST.
    return GraphicsOperationResult::Succeeded;
}

HRESULT createSwapChainResources(DeviceResources& resources,
    const TargetDescriptor& descriptor,
    TargetRealization& target,
    const DesktopSceneEngine::Platform::Windows::NativeClientSize& clientSize,
    bool publishedSlot = false,
    HRESULT* requiredRollbackResult = nullptr)
{
    if (std::holds_alternative<HwndSwapChainResources>(target.transport))
        return createHwndSwapChainResources(resources, descriptor, target, clientSize);
    const HRESULT creationResult =
        createCompositionSwapChainResources(resources, descriptor, target, clientSize);
    if (SUCCEEDED(creationResult))
        return creationResult;
    const bool acceptedSharedMutation =
        std::get<CompositionSwapChainResources>(target.transport).treeState
            != CompositionTreeState::Unwired;
    const HRESULT disconnectResult = releasePresentationDrawableResources(resources, target);
    if (requiredRollbackResult)
        *requiredRollbackResult = disconnectResult;
    const bool deviceLost = classifyDeviceOperation(resources, creationResult)
            == GraphicsOperationResult::DeviceLost
        || classifyDeviceOperation(resources, disconnectResult)
            == GraphicsOperationResult::DeviceLost;
    if (publishedSlot && !deviceLost
        && (FAILED(disconnectResult)
            || (IsWindow(reinterpret_cast<HWND>(descriptor.nativeHandle))
                && (acceptedSharedMutation || !resources.compositionDevice))))
        enterCompositionContainment(resources);
    return classifyDeviceOperation(resources, disconnectResult)
        == GraphicsOperationResult::DeviceLost ? disconnectResult : creationResult;
}

bool createPresentationTargetResources(
    DeviceResources& deviceResources,
    const TargetDescriptor& descriptor,
    TargetRealization& target,
    bool publishedSlot = false,
    HRESULT* failureResult = nullptr,
    HRESULT* requiredRollbackResult = nullptr)
{
    if (requiredRollbackResult)
        *requiredRollbackResult = S_OK;
    target.id = descriptor.id;
    if (descriptor.transport == PresentationTransport::DirectCompositionSwapChain)
        target.transport.emplace<CompositionSwapChainResources>();
    const auto clientSize =
        DesktopSceneEngine::Platform::Windows::queryNativeWindowClientSize(
            descriptor.nativeHandle);
    if (!clientSize.has_value())
    {
        if (failureResult)
            *failureResult = E_HANDLE;
        target.state = PresentationTargetState::Failed;
        return false;
    }

    target.observedClientWidth = clientSize->width;
    target.observedClientHeight = clientSize->height;
    if (clientSize->width == 0 || clientSize->height == 0)
    {
        target.state = PresentationTargetState::Suspended;
        return true;
    }

    const HRESULT result = createSwapChainResources(deviceResources, descriptor, target,
        *clientSize, publishedSlot, requiredRollbackResult);
    if (FAILED(result))
    {
        if (failureResult)
            *failureResult = result;
        target.state = PresentationTargetState::Failed;
        return false;
    }

    return true;
}

HRESULT releasePresentationTargetResources(
    DeviceResources& deviceResources,
    TargetRealization& target,
    bool allowCompositionDisconnect = true
) noexcept
{
    const HRESULT requiredTransactionResult =
        releasePresentationDrawableResources(deviceResources, target, allowCompositionDisconnect);
    target.observedClientWidth = 0;
    target.observedClientHeight = 0;
    target.realizedClientWidth = 0;
    target.realizedClientHeight = 0;
    target.successfulResizeCountForTesting = 0U;
    target.forcedPresentResultForTesting = 0;
    target.forcedDeviceRemovedReasonForTesting = 0;
    target.failNextRenderTargetViewCreationForTesting = false;
    target.hasForcedPresentResultForTesting = false;
    target.pendingTargetLocalRecovery = false;
    target.state = PresentationTargetState::Detached;
    return requiredTransactionResult;
}

struct PresentationResult
{
    HRESULT result = S_OK;
    HRESULT deviceRemovedReason = S_OK;
};

PresentationResult presentPresentationTarget(
    DeviceResources& deviceResources,
    TargetRealization& target,
    UINT syncInterval,
    UINT flags) noexcept
{
    const HRESULT actualResult = target.swapChain->Present(syncInterval, flags);
    const HRESULT actualDeviceRemovedReason =
        FAILED(actualResult) && deviceResources.device
        ? deviceResources.device->GetDeviceRemovedReason()
        : S_OK;
    if (DesktopSceneEngine::Rendering::Detail::classifyPresentResult(
            static_cast<std::int32_t>(actualResult),
            static_cast<std::int32_t>(actualDeviceRemovedReason))
        == DesktopSceneEngine::Rendering::Detail::PresentResultCategory::DeviceLost)
    {
        return {actualResult, actualDeviceRemovedReason};
    }

    if (std::exchange(target.hasForcedPresentResultForTesting, false))
    {
        return {
            static_cast<HRESULT>(target.forcedPresentResultForTesting),
            static_cast<HRESULT>(
                target.forcedDeviceRemovedReasonForTesting)};
    }

    return {actualResult, actualDeviceRemovedReason};
}

bool isDeviceLost(const DeviceResources& deviceResources) noexcept
{
    return deviceResources.device
        && FAILED(deviceResources.device->GetDeviceRemovedReason());
}

bool reconcilePresentationTarget(
    DeviceResources& deviceResources,
    const TargetDescriptor& descriptor,
    TargetRealization& target,
    bool& generationLost,
    HRESULT& requiredDisconnectResult)
{
    requiredDisconnectResult = S_OK;
    const bool wasUsable = target.state == PresentationTargetState::Ready
        || target.state == PresentationTargetState::Occluded
        || target.state == PresentationTargetState::Suspended;
    const auto clientSize =
        DesktopSceneEngine::Platform::Windows::queryNativeWindowClientSize(
            descriptor.nativeHandle);
    if (!clientSize.has_value())
    {
        target.pendingTargetLocalRecovery = false; // Endpoint owner must resolve invalid HWND.
        deviceResources.context->OMSetRenderTargets(0, nullptr, nullptr);
        target.renderTargetView.Reset();
        if (std::holds_alternative<CompositionSwapChainResources>(target.transport))
        {
            requiredDisconnectResult = releasePresentationDrawableResources(
                deviceResources, target);
        }
        target.state = PresentationTargetState::Failed;
        return false;
    }

    const bool hasDrawableNativeSize =
        clientSize->width > 0 && clientSize->height > 0;
    const bool nativeSizeChanged =
        clientSize->width != target.observedClientWidth
        || clientSize->height != target.observedClientHeight;
    const bool realizedSizeChanged =
        clientSize->width != target.realizedClientWidth
        || clientSize->height != target.realizedClientHeight;
    target.observedClientWidth = clientSize->width;
    target.observedClientHeight = clientSize->height;
    if (!target.pendingTargetLocalRecovery && hasDrawableNativeSize && nativeSizeChanged
        && (target.state == PresentationTargetState::Failed
            || (target.state == PresentationTargetState::Suspended
                && (!target.swapChain || !target.renderTargetView))))
        target.pendingTargetLocalRecovery = true;

    if (!hasDrawableNativeSize)
    {
        target.state = PresentationTargetState::Suspended;
        return false;
    }

    if (!DesktopSceneEngine::Rendering::Detail::
            shouldContinuePresentationTargetReconciliation(
                target.state,
                hasDrawableNativeSize,
                target.pendingTargetLocalRecovery))
    {
        return false;
    }

    if (target.state == PresentationTargetState::Occluded
        && !realizedSizeChanged && target.renderTargetView)
        return false;

    const auto failLocalAttempt = [&](HRESULT result, bool authorizedAttempt)
    {
        target.state = PresentationTargetState::Failed;
        const auto category = classifyDeviceOperation(deviceResources, result);
        if (category == GraphicsOperationResult::DeviceLost)
        {
            generationLost = true;
            return;
        }
        if (category == GraphicsOperationResult::ResourceFailure
            && !authorizedAttempt && wasUsable
            && IsWindow(reinterpret_cast<HWND>(descriptor.nativeHandle))
            && (descriptor.transport == PresentationTransport::HwndSwapChain
                || deviceResources.compositionTrustState == CompositionTrustState::Trusted))
            target.pendingTargetLocalRecovery = true;
    };

    if (!target.swapChain)
    {
        // Spend only at actual work, before the first native creation call.
        const bool authorizedAttempt = std::exchange(target.pendingTargetLocalRecovery, false);
        const HRESULT result = createSwapChainResources(deviceResources, descriptor,
            target, *clientSize, true);
        if (FAILED(result))
        {
            failLocalAttempt(result, authorizedAttempt);
            return false;
        }

        return target.state == PresentationTargetState::Ready;
    }

    if (!realizedSizeChanged)
    {
        if (!target.renderTargetView)
        {
            const bool authorizedAttempt = std::exchange(target.pendingTargetLocalRecovery, false);
            ComPtr<ID3D11RenderTargetView> replacementView{};
            const bool forceRenderTargetViewFailure = std::exchange(
                target.failNextRenderTargetViewCreationForTesting, false);
            const HRESULT result = forceRenderTargetViewFailure ? E_FAIL
                : createRenderTargetView(deviceResources, target.swapChain.Get(), replacementView);
            if (FAILED(result))
            {
                failLocalAttempt(result, authorizedAttempt);
                return false;
            }

            target.renderTargetView = std::move(replacementView);
            ++target.generation;
        }

        if (target.pendingTargetLocalRecovery)
            return false; // No native reconstruction occurred; preserve authority.
        target.state = hasDrawableViewport(descriptor, target)
            ? PresentationTargetState::Ready
            : PresentationTargetState::Suspended;
        return target.state == PresentationTargetState::Ready;
    }

    const bool authorizedAttempt = std::exchange(target.pendingTargetLocalRecovery, false);
    deviceResources.context->OMSetRenderTargets(0, nullptr, nullptr);
    target.renderTargetView.Reset();
    const HRESULT resizeResult = target.swapChain->ResizeBuffers(
            0,
            static_cast<UINT>(clientSize->width),
            static_cast<UINT>(clientSize->height),
            DXGI_FORMAT_UNKNOWN,
            0);
    if (FAILED(resizeResult))
    {
        failLocalAttempt(resizeResult, authorizedAttempt);
        return false;
    }

    ++target.successfulResizeCountForTesting;
    target.realizedClientWidth = clientSize->width;
    target.realizedClientHeight = clientSize->height;

    ComPtr<ID3D11RenderTargetView> replacementView{};
    const bool forceRenderTargetViewFailure = std::exchange(
        target.failNextRenderTargetViewCreationForTesting,
        false);
    const HRESULT viewResult = forceRenderTargetViewFailure ? E_FAIL
        : createRenderTargetView(deviceResources, target.swapChain.Get(), replacementView);
    if (FAILED(viewResult))
    {
        failLocalAttempt(viewResult, authorizedAttempt);
        return false;
    }

    target.renderTargetView = std::move(replacementView);
    ++target.generation;
    target.state = hasDrawableViewport(descriptor, target)
        ? PresentationTargetState::Ready
        : PresentationTargetState::Suspended;
    return target.state == PresentationTargetState::Ready;
}

bool bindPresentationTarget(
    DeviceResources& deviceResources,
    const TargetDescriptor& descriptor,
    const TargetRealization& target)
{
    if (!deviceResources.context
        || !isTargetEffectivelyReady(deviceResources, target)
        || !target.renderTargetView
        || !hasDrawableViewport(descriptor, target))
    {
        return false;
    }

    if (const auto* composition = std::get_if<CompositionSwapChainResources>(&target.transport);
        composition && (!composition->target || !composition->visual
            || composition->treeState != CompositionTreeState::Submitted))
        return false;

    ID3D11RenderTargetView* renderTarget = target.renderTargetView.Get();
    deviceResources.context->OMSetRenderTargets(1, &renderTarget, nullptr);
    return true;
}

bool updatePresentationConstants(
    DeviceResources& deviceResources,
    const CurrentMediaRealization& media,
    const DesktopSceneEngine::Rendering::RenderPresentation& presentation)
{
    if (!deviceResources.context
        || !deviceResources.presentationConstantBuffer
        || !media.shaderResourceView
        || !hasValidPresentation(presentation))
    {
        return false;
    }

    const PresentationConstants constants{
        {presentation.sourceUv.x, presentation.sourceUv.y,
         presentation.sourceUv.width, presentation.sourceUv.height},
        {presentation.destination.x, presentation.destination.y,
         presentation.destination.width, presentation.destination.height}};
    deviceResources.context->UpdateSubresource(
        deviceResources.presentationConstantBuffer.Get(),
        0,
        nullptr,
        &constants,
        0,
        0);
    return true;
}

void drawSharedImage(
    DeviceResources& deviceResources,
    const CurrentMediaRealization& media)
{
    ID3D11ShaderResourceView* resourceView =
        media.shaderResourceView.Get();
    ID3D11SamplerState* samplerState = deviceResources.samplerState.Get();
    ID3D11Buffer* presentationConstantBuffer =
        deviceResources.presentationConstantBuffer.Get();
    deviceResources.context->IASetInputLayout(nullptr);
    deviceResources.context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    deviceResources.context->VSSetShader(
        deviceResources.vertexShader.Get(), nullptr, 0);
    deviceResources.context->VSSetConstantBuffers(
        0, 1, &presentationConstantBuffer);
    deviceResources.context->PSSetShader(
        deviceResources.pixelShader.Get(), nullptr, 0);
    deviceResources.context->PSSetShaderResources(0, 1, &resourceView);
    deviceResources.context->PSSetSamplers(0, 1, &samplerState);
    deviceResources.context->Draw(4, 0);

    ID3D11ShaderResourceView* emptyResourceView = nullptr;
    deviceResources.context->PSSetShaderResources(0, 1, &emptyResourceView);
}

void captureRenderedPixel(
    DeviceResources& resources,
    const TargetDescriptor& descriptor,
    TargetRealization& target)
{
    if (!std::exchange(target.captureNextPixelForTesting, false))
        return;
    target.capturedPixelValidForTesting = false;
    ComPtr<ID3D11Resource> renderedResource;
    target.renderTargetView->GetResource(&renderedResource);
    D3D11_TEXTURE2D_DESC description{};
    description.Width = 1;
    description.Height = 1;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(resources.device->CreateTexture2D(&description, nullptr, &staging)))
        return;
    const UINT x = static_cast<UINT>(target.capturePixelXForTesting < 0
        ? target.realizedClientWidth / 2 : target.capturePixelXForTesting);
    const UINT y = static_cast<UINT>(target.capturePixelYForTesting < 0
        ? viewportHeight(descriptor, target) / 2 : target.capturePixelYForTesting);
    const D3D11_BOX region{x, y, 0, x + 1, y + 1, 1};
    resources.context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0,
        renderedResource.Get(), 0, &region);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(resources.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    {
        std::memcpy(&target.capturedPixelRgbaForTesting, mapped.pData, sizeof(std::uint32_t));
        resources.context->Unmap(staging.Get(), 0);
        target.capturedPixelValidForTesting = true;
    }
}

}

namespace DesktopSceneEngine::Rendering
{

Detail::GraphicsOperationResult Detail::classifyGraphicsOperationResult(
    std::int32_t operationResult,
    std::int32_t deviceRemovedReason) noexcept
{
    const HRESULT result = static_cast<HRESULT>(operationResult);
    const HRESULT removedReason = static_cast<HRESULT>(deviceRemovedReason);
    if (result == DXGI_ERROR_DEVICE_REMOVED
        || result == DXGI_ERROR_DEVICE_RESET
        || result == DXGI_ERROR_DEVICE_HUNG
        || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR
#if defined(D3DDDIERR_DEVICEREMOVED)
        || result == D3DDDIERR_DEVICEREMOVED
#endif
        || FAILED(removedReason))
    {
        return GraphicsOperationResult::DeviceLost;
    }

    return FAILED(result)
        ? GraphicsOperationResult::ResourceFailure
        : GraphicsOperationResult::Succeeded;
}

Detail::PresentResultCategory Detail::classifyPresentResult(
    std::int32_t presentResult,
    std::int32_t deviceRemovedReason) noexcept
{
    const HRESULT result = static_cast<HRESULT>(presentResult);
    if (classifyGraphicsOperationResult(presentResult, deviceRemovedReason)
        == GraphicsOperationResult::DeviceLost)
    {
        return PresentResultCategory::DeviceLost;
    }

    if (result == S_OK)
    {
        return PresentResultCategory::Presented;
    }

    if (result == DXGI_STATUS_OCCLUDED)
    {
        return PresentResultCategory::Occluded;
    }

    if (result == DXGI_ERROR_WAS_STILL_DRAWING)
    {
        return PresentResultCategory::BusySkipped;
    }

    return FAILED(result)
        ? PresentResultCategory::TargetFailed
        : PresentResultCategory::Presented;
}

struct GraphicsBackend::Impl
{
    std::vector<TargetDescriptor> targetDescriptors{};
    std::unique_ptr<GraphicsGeneration> currentGeneration{};
    PresentationTargetId preferredPacingTargetId{};
    std::uint64_t nextPresentationTargetId = 1U;
    std::uint64_t nextDeviceGenerationForTesting = 1U;
    std::uint64_t graphicsGenerationCreationCountForTesting = 0U;
    std::uint64_t graphicsGenerationDestructionCountForTesting = 0U;
    std::uint64_t presentOrder = 0U;
    std::uint64_t retiredMediaUploadCountForTesting = 0U;
    std::uint64_t retiredCompositionCapabilityCreationCountForTesting = 0U;
    std::uint64_t retiredCompositionCommitCountForTesting = 0U;
    std::uint64_t retiredCompositionWaitCountForTesting = 0U;
    HRESULT lastRetiredCompositionDisconnectResultForTesting = S_OK;
    bool initialized = false;
    bool targetIdExhausted = false;
    bool pendingGenerationRecovery = false;
    PresentationTargetId forcedTargetRealizationIdForTesting{};
    std::int32_t forcedTargetOperationResultForTesting = 0;
    std::int32_t forcedTargetDeviceRemovedReasonForTesting = 0;
    PresentationTargetId forceDeviceLostOnDetachIdForTesting{};
    bool failNextGenerationCreationForTesting = false;
    std::uint64_t graphicsRecoveryAttemptCountForTesting = 0U;
    std::int32_t forcedMediaOperationResultForTesting = 0;
    std::int32_t forcedMediaDeviceRemovedReasonForTesting = 0;
    bool hasForcedMediaUpdateResultForTesting = false;

    [[nodiscard]] TargetDescriptor* findDescriptor(PresentationTargetId id) noexcept
    {
        const auto found = std::find_if(targetDescriptors.begin(), targetDescriptors.end(),
            [id](const TargetDescriptor& descriptor) { return descriptor.id == id; });
        return found != targetDescriptors.end() ? &*found : nullptr;
    }

    [[nodiscard]] const TargetDescriptor* findDescriptor(PresentationTargetId id) const noexcept
    {
        const auto found = std::find_if(targetDescriptors.begin(), targetDescriptors.end(),
            [id](const TargetDescriptor& descriptor) { return descriptor.id == id; });
        return found != targetDescriptors.end() ? &*found : nullptr;
    }

    [[nodiscard]] TargetRealization* findRealization(PresentationTargetId id) noexcept
    {
        if (!currentGeneration)
            return nullptr;
        const auto found = std::find_if(currentGeneration->targets.begin(), currentGeneration->targets.end(),
            [id](const TargetRealization& target) { return target.id == id; });
        return found != currentGeneration->targets.end() ? &*found : nullptr;
    }

    [[nodiscard]] const TargetRealization* findRealization(PresentationTargetId id) const noexcept
    {
        if (!currentGeneration)
            return nullptr;
        const auto found = std::find_if(currentGeneration->targets.begin(), currentGeneration->targets.end(),
            [id](const TargetRealization& target) { return target.id == id; });
        return found != currentGeneration->targets.end() ? &*found : nullptr;
    }

    [[nodiscard]] PresentationTargetId usablePacingTargetId() const noexcept
    {
        if (!currentGeneration)
            return {};
        const auto& resources = currentGeneration->deviceResources;
        if (const auto* preferred = findRealization(preferredPacingTargetId);
            preferred && isTargetEffectivelyReady(resources, *preferred))
            return preferred->id;
        const auto fallback = std::find_if(currentGeneration->targets.begin(), currentGeneration->targets.end(),
            [&resources](const TargetRealization& target)
            { return isTargetEffectivelyReady(resources, target); });
        return fallback != currentGeneration->targets.end() ? fallback->id : PresentationTargetId{};
    }

    void discardGeneration(
        std::unique_ptr<GraphicsGeneration> generation,
        bool allowCompositionDisconnect) noexcept
    {
        if (!generation)
            return;
        for (auto& target : generation->targets)
            releasePresentationTargetResources(
                generation->deviceResources, target, allowCompositionDisconnect);
        retiredMediaUploadCountForTesting += generation->currentMedia.uploadCountForTesting;
        retiredCompositionCapabilityCreationCountForTesting +=
            generation->deviceResources.compositionCapabilityCreationCountForTesting;
        retiredCompositionCommitCountForTesting +=
            generation->deviceResources.compositionCommitCountForTesting;
        retiredCompositionWaitCountForTesting +=
            generation->deviceResources.compositionWaitCountForTesting;
        lastRetiredCompositionDisconnectResultForTesting =
            generation->deviceResources.lastCompositionDisconnectResultForTesting;
        generation.reset();
        ++graphicsGenerationDestructionCountForTesting;
    }

    void retireGeneration(bool allowCompositionDisconnect) noexcept
    {
        discardGeneration(std::move(currentGeneration), allowCompositionDisconnect);
    }

    void transitionToPendingRecovery() noexcept
    {
        retireGeneration(false);
        pendingGenerationRecovery = true;
    }

    [[nodiscard]] std::optional<Detail::MediaUpdateResult>
    consumeForcedMediaUpdateResultForTesting() noexcept
    {
        if (!std::exchange(hasForcedMediaUpdateResultForTesting, false))
            return std::nullopt;
        const auto classified = Detail::classifyGraphicsOperationResult(
            forcedMediaOperationResultForTesting,
            forcedMediaDeviceRemovedReasonForTesting);
        if (classified == Detail::GraphicsOperationResult::DeviceLost)
            return Detail::MediaUpdateResult::DeviceLost;
        if (classified == Detail::GraphicsOperationResult::ResourceFailure)
            return Detail::MediaUpdateResult::ResourceFailure;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Detail::GraphicsOperationResult>
    consumeForcedTargetRealizationResultForTesting(
        PresentationTargetId id) noexcept
    {
        if (forcedTargetRealizationIdForTesting != id)
            return std::nullopt;
        forcedTargetRealizationIdForTesting = {};
        return Detail::classifyGraphicsOperationResult(
            forcedTargetOperationResultForTesting,
            forcedTargetDeviceRemovedReasonForTesting);
    }
};

GraphicsBackend::GraphicsBackend()
    : impl_(std::make_unique<Impl>())
{
}

GraphicsBackend::~GraphicsBackend()
{
    shutdown();
}

GraphicsBackend::PresentationTargetInfo
GraphicsBackend::presentationTargetInfo() const noexcept
{
    if (!impl_ || !impl_->initialized || impl_->targetDescriptors.empty())
        return {};

    PresentationTargetId selected = impl_->usablePacingTargetId();
    if (!selected.isValid())
        selected = impl_->preferredPacingTargetId.isValid()
            ? impl_->preferredPacingTargetId : impl_->targetDescriptors.front().id;
    return presentationTargetInfo(selected);
}

GraphicsBackend::PresentationTargetInfo GraphicsBackend::presentationTargetInfo(
    PresentationTargetId id) const noexcept
{
    if (!impl_ || !impl_->initialized || !id.isValid())
    {
        return {};
    }

    const auto* descriptor = impl_->findDescriptor(id);
    const auto* target = impl_->findRealization(id);
    if (!descriptor || !target || !impl_->currentGeneration)
        return {};

    return {
        target->realizedClientWidth,
        target->realizedClientHeight,
        target->realizedClientWidth,
        viewportHeight(*descriptor, *target),
        impl_->currentGeneration->deviceResources.generation,
        target->generation,
        isTargetEffectivelyReady(impl_->currentGeneration->deviceResources, *target),
        target->state == PresentationTargetState::Suspended,
        target->state == PresentationTargetState::Occluded,
        target->state == PresentationTargetState::Failed};
}

GraphicsBackend::PresentationTargetDiagnostics
GraphicsBackend::presentationTargetDiagnosticsForTesting(
    PresentationTargetId id) const noexcept
{
    if (!impl_ || !id.isValid())
    {
        return {};
    }

    const auto* descriptor = impl_->findDescriptor(id);
    const auto* target = impl_->findRealization(id);
    if (!descriptor || !target || !impl_->currentGeneration)
        return {};

    const auto* composition = std::get_if<CompositionSwapChainResources>(&target->transport);
    const auto& resources = impl_->currentGeneration->deviceResources;
    return {
        target->drawCountForTesting,
        target->presentAttemptCountForTesting,
        target->lastPresentOrderForTesting,
        target->lastPresentSyncIntervalForTesting,
        target->lastPresentFlagsForTesting,
        descriptor->transport,
        composition && composition->target,
        composition && composition->visual,
        composition && composition->treeState == CompositionTreeState::Submitted,
        target->successfulResizeCountForTesting,
        target->capturedPixelRgbaForTesting,
        target->capturedPixelValidForTesting,
        reinterpret_cast<std::uintptr_t>(resources.device.Get()),
        reinterpret_cast<std::uintptr_t>(target->swapChain.Get()),
        composition ? reinterpret_cast<std::uintptr_t>(composition->target.Get()) : 0U,
        composition ? reinterpret_cast<std::uintptr_t>(composition->visual.Get()) : 0U};
}

void GraphicsBackend::failNextCompositionOperationForTesting(
    Detail::CompositionFailurePoint point) noexcept
{
    (void)forceNextCompositionOperationResultForTesting(
        point, static_cast<std::int32_t>(E_FAIL));
}

bool GraphicsBackend::forceNextCompositionOperationResultForTesting(
    Detail::CompositionFailurePoint point, std::int32_t operationResult) noexcept
{
    if (!impl_ || !impl_->currentGeneration
        || point == Detail::CompositionFailurePoint::None
        || SUCCEEDED(static_cast<HRESULT>(operationResult)))
        return false;
    auto& resources = impl_->currentGeneration->deviceResources;
    resources.compositionFailureForTesting = point;
    resources.compositionFailureResultForTesting = static_cast<HRESULT>(operationResult);
    return true;
}

bool GraphicsBackend::forceNextCompositionRollbackResultForTesting(
    Detail::CompositionFailurePoint point, std::int32_t operationResult) noexcept
{
    if (!impl_ || !impl_->currentGeneration
        || (point != Detail::CompositionFailurePoint::DisconnectSetRoot
            && point != Detail::CompositionFailurePoint::DisconnectSetContent
            && point != Detail::CompositionFailurePoint::DisconnectCommit
            && point != Detail::CompositionFailurePoint::DisconnectWait)
        || SUCCEEDED(static_cast<HRESULT>(operationResult)))
        return false;
    auto& resources = impl_->currentGeneration->deviceResources;
    resources.compositionRollbackFailureForTesting = point;
    resources.compositionRollbackFailureResultForTesting = static_cast<HRESULT>(operationResult);
    return true;
}

GraphicsBackend::CompositionDiagnostics GraphicsBackend::compositionDiagnosticsForTesting()
    const noexcept
{
    if (!impl_)
        return {};
    const auto* resources = impl_->currentGeneration
        ? &impl_->currentGeneration->deviceResources : nullptr;
    CompositionDiagnostics diagnostics{
        resources ? reinterpret_cast<std::uintptr_t>(resources->compositionDevice.Get()) : 0U,
        impl_->retiredCompositionCapabilityCreationCountForTesting
            + (resources ? resources->compositionCapabilityCreationCountForTesting : 0U),
        impl_->retiredCompositionCommitCountForTesting
            + (resources ? resources->compositionCommitCountForTesting : 0U),
        impl_->retiredCompositionWaitCountForTesting
            + (resources ? resources->compositionWaitCountForTesting : 0U),
        static_cast<std::int32_t>(resources
            ? resources->lastCompositionDisconnectResultForTesting
            : impl_->lastRetiredCompositionDisconnectResultForTesting)};
    if (!resources)
        return diagnostics;
    diagnostics.armedFailurePoint = resources->compositionFailureForTesting;
    diagnostics.armedRollbackFailurePoint = resources->compositionRollbackFailureForTesting;
    switch (resources->compositionTrustState)
    {
    case CompositionTrustState::Trusted:
        diagnostics.trustState = CompositionDiagnostics::TrustState::Trusted;
        break;
    case CompositionTrustState::UntrustedPending:
        diagnostics.trustState = CompositionDiagnostics::TrustState::UntrustedPending;
        break;
    case CompositionTrustState::UntrustedSpent:
        diagnostics.trustState = CompositionDiagnostics::TrustState::UntrustedSpent;
        break;
    }
    return diagnostics;
}

bool GraphicsBackend::captureNextPresentationPixelForTesting(PresentationTargetId id) noexcept
{
    return captureNextPresentationPixelForTesting(id, -1, -1);
}

bool GraphicsBackend::captureNextPresentationPixelForTesting(
    PresentationTargetId id, int x, int y) noexcept
{
    if (!impl_ || !impl_->initialized || !impl_->currentGeneration
        || isDeviceLost(impl_->currentGeneration->deviceResources))
        return false;
    auto* found = impl_->findRealization(id);
    if (!found || !isTargetEffectivelyReady(impl_->currentGeneration->deviceResources, *found))
        return false;
    if (!((x == -1 && y == -1) || (x >= 0 && y >= 0
            && x < found->realizedClientWidth && y < found->realizedClientHeight)))
        return false;
    found->captureNextPixelForTesting = true;
    found->capturePixelXForTesting = x;
    found->capturePixelYForTesting = y;
    found->capturedPixelValidForTesting = false;
    return true;
}

std::size_t GraphicsBackend::presentationTargetCountForTesting() const noexcept
{
    return impl_ ? impl_->targetDescriptors.size() : 0U;
}

PresentationTargetId GraphicsBackend::pacingPresentationTargetForTesting()
    const noexcept
{
    if (!impl_)
        return {};
    if (const auto* preferred = impl_->findRealization(impl_->preferredPacingTargetId);
        preferred && preferred->state != PresentationTargetState::Failed
            && (!std::holds_alternative<CompositionSwapChainResources>(preferred->transport)
                || (impl_->currentGeneration
                    && impl_->currentGeneration->deviceResources.compositionTrustState
                        == CompositionTrustState::Trusted)))
        return preferred->id;
    return impl_->usablePacingTargetId();
}

PresentationTargetId GraphicsBackend::preferredPacingTargetForTesting()
    const noexcept
{
    return impl_ ? impl_->preferredPacingTargetId : PresentationTargetId{};
}

bool GraphicsBackend::hasCurrentGraphicsGenerationForTesting() const noexcept
{
    return impl_ && impl_->currentGeneration != nullptr;
}

bool GraphicsBackend::pendingGraphicsRecoveryForTesting() const noexcept
{
    return impl_ && impl_->pendingGenerationRecovery;
}

bool GraphicsBackend::pendingTargetLocalRecoveryForTesting(
    PresentationTargetId id) const noexcept
{
    const auto* realization = impl_ ? impl_->findRealization(id) : nullptr;
    return realization && realization->pendingTargetLocalRecovery;
}

std::uint64_t GraphicsBackend::graphicsGenerationCreationCountForTesting()
    const noexcept
{
    return impl_ ? impl_->graphicsGenerationCreationCountForTesting : 0U;
}

std::uint64_t GraphicsBackend::graphicsGenerationDestructionCountForTesting()
    const noexcept
{
    return impl_ ? impl_->graphicsGenerationDestructionCountForTesting : 0U;
}

std::uint64_t GraphicsBackend::graphicsRecoveryAttemptCountForTesting()
    const noexcept
{
    return impl_ ? impl_->graphicsRecoveryAttemptCountForTesting : 0U;
}

bool GraphicsBackend::hasCurrentMediaRealizationForTesting() const noexcept
{
    return impl_ && impl_->currentGeneration
        && impl_->currentGeneration->currentMedia.texture
        && impl_->currentGeneration->currentMedia.shaderResourceView;
}

void GraphicsBackend::failNextTargetRealizationForTesting(
    PresentationTargetId id) noexcept
{
    (void)forceNextTargetRealizationResultForTesting(
        id, static_cast<std::int32_t>(E_FAIL), static_cast<std::int32_t>(S_OK));
}

bool GraphicsBackend::forceNextTargetRealizationResultForTesting(
    PresentationTargetId id,
    std::int32_t operationResult,
    std::int32_t deviceRemovedReason) noexcept
{
    if (!impl_ || !id.isValid()
        || (id.value_ != impl_->nextPresentationTargetId
            && !impl_->findDescriptor(id))
        || Detail::classifyGraphicsOperationResult(
                operationResult, deviceRemovedReason)
            == Detail::GraphicsOperationResult::Succeeded)
        return false;
    impl_->forcedTargetRealizationIdForTesting = id;
    impl_->forcedTargetOperationResultForTesting = operationResult;
    impl_->forcedTargetDeviceRemovedReasonForTesting = deviceRemovedReason;
    return true;
}

bool GraphicsBackend::forceDeviceLostOnTargetDetachForTesting(
    PresentationTargetId id) noexcept
{
    if (!impl_ || !impl_->currentGeneration || !impl_->findDescriptor(id))
        return false;
    impl_->forceDeviceLostOnDetachIdForTesting = id;
    return true;
}

void GraphicsBackend::failNextGraphicsGenerationCreationForTesting() noexcept
{
    if (impl_)
        impl_->failNextGenerationCreationForTesting = true;
}

void GraphicsBackend::forceNextMediaUpdateResultForTesting(
    std::int32_t operationResult,
    std::int32_t deviceRemovedReason) noexcept
{
    if (!impl_)
        return;
    impl_->forcedMediaOperationResultForTesting = operationResult;
    impl_->forcedMediaDeviceRemovedReasonForTesting = deviceRemovedReason;
    impl_->hasForcedMediaUpdateResultForTesting = true;
}

std::uint64_t GraphicsBackend::mediaUploadCountForTesting() const noexcept
{
    return impl_ ? impl_->retiredMediaUploadCountForTesting
        + (impl_->currentGeneration
            ? impl_->currentGeneration->currentMedia.uploadCountForTesting : 0U)
        : 0U;
}

std::uint64_t GraphicsBackend::nextPresentationTargetValueForTesting()
    const noexcept
{
    return impl_ ? impl_->nextPresentationTargetId : 0U;
}

PresentationTargetId GraphicsBackend::nextPresentationTargetIdForTesting()
    const noexcept
{
    return impl_ && !impl_->targetIdExhausted
        ? PresentationTargetId{impl_->nextPresentationTargetId}
        : PresentationTargetId{};
}

bool GraphicsBackend::forceNextDxgiPresentResultForTesting(
    PresentationTargetId id,
    std::int32_t presentResult,
    std::int32_t deviceRemovedReason) noexcept
{
    if (!impl_ || !impl_->initialized || !id.isValid())
    {
        return false;
    }

    auto* found = impl_->findRealization(id);
    if (!found || !found->swapChain)
    {
        return false;
    }

    found->forcedPresentResultForTesting = presentResult;
    found->forcedDeviceRemovedReasonForTesting = deviceRemovedReason;
    found->hasForcedPresentResultForTesting = true;
    return true;
}

void GraphicsBackend::failNextPresentationRenderTargetViewCreationForTesting()
    noexcept
{
    if (impl_ && impl_->currentGeneration
        && !impl_->currentGeneration->targets.empty())
    {
        auto* found = impl_->findRealization(impl_->usablePacingTargetId());
        if (!found)
            found = &impl_->currentGeneration->targets.front();
        found->failNextRenderTargetViewCreationForTesting = true;
    }
}

std::uint64_t GraphicsBackend::successfulPresentationResizeCountForTesting()
    const noexcept
{
    if (!impl_ || !impl_->currentGeneration
        || impl_->currentGeneration->targets.empty())
        return 0U;
    const auto* found = impl_->findRealization(impl_->usablePacingTargetId());
    return (found ? found : &impl_->currentGeneration->targets.front())
        ->successfulResizeCountForTesting;
}

PresentationTargetId GraphicsBackend::initialize(const WindowTarget& target)
{
    if (!impl_)
    {
        impl_ = std::make_unique<Impl>();
    }

    if (impl_->initialized)
    {
        return {};
    }

    auto generation = std::make_unique<GraphicsGeneration>();
    if (!validTransport(target.transport) || target.handle == nullptr
        || !Platform::Windows::queryNativeWindowClientSize(target.handle).has_value()
        || !createDeviceResources(generation->deviceResources))
    {
        shutdown();
        return {};
    }
    generation->deviceResources.generation = impl_->nextDeviceGenerationForTesting++;
    impl_->currentGeneration = std::move(generation);
    ++impl_->graphicsGenerationCreationCountForTesting;
    impl_->pendingGenerationRecovery = false;
    impl_->initialized = true;
    const PresentationTargetId id = attachPresentationTarget(target);
    if (!id.isValid())
    {
        shutdown();
    }
    return id;
}

PresentationTargetId GraphicsBackend::attachPresentationTarget(
    const WindowTarget& target)
{
    if (!impl_ || !impl_->initialized
        || target.handle == nullptr || impl_->targetIdExhausted)
    {
        return {};
    }

    if (!validTransport(target.transport)
        || !Platform::Windows::queryNativeWindowClientSize(target.handle).has_value())
        return {};

    if (target.transport == PresentationTransport::DirectCompositionSwapChain
        && impl_->currentGeneration
        && impl_->currentGeneration->deviceResources.compositionTrustState
            != CompositionTrustState::Trusted)
        return {};

    if (std::any_of(
            impl_->targetDescriptors.begin(),
            impl_->targetDescriptors.end(),
            [&target](const TargetDescriptor& existing)
            {
                return existing.nativeHandle == target.handle;
            }))
    {
        return {};
    }

    // Reserve before creating native resources or submitting a composition tree.
    // Record moves are noexcept, so publication cannot allocate/fail afterwards.
    try
    {
        if (impl_->targetDescriptors.size() == impl_->targetDescriptors.max_size()
            || (impl_->currentGeneration
                && impl_->currentGeneration->targets.size()
                    == impl_->currentGeneration->targets.max_size()))
            return {};
        impl_->targetDescriptors.reserve(impl_->targetDescriptors.size() + 1U);
        if (impl_->currentGeneration)
            impl_->currentGeneration->targets.reserve(
                impl_->currentGeneration->targets.size() + 1U);
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }

    const PresentationTargetId id{impl_->nextPresentationTargetId};
    const TargetDescriptor descriptor{id, target.handle, target.transport,
        (std::max)(target.bottomInsetPixels, 0)};
    TargetRealization candidate{};
    bool realized = false;
    bool generationLost = false;
    if (impl_->currentGeneration)
    {
        auto& generation = *impl_->currentGeneration;
        generationLost = isDeviceLost(generation.deviceResources);
        if (!generationLost)
        {
            const auto forcedResult =
                impl_->consumeForcedTargetRealizationResultForTesting(id);
            generationLost = forcedResult
                && *forcedResult == Detail::GraphicsOperationResult::DeviceLost;
            HRESULT realizationFailure = S_OK;
            HRESULT requiredRollbackResult = S_OK;
            realized = !forcedResult
                && createPresentationTargetResources(
                    generation.deviceResources, descriptor, candidate, false,
                    &realizationFailure, &requiredRollbackResult);
            if (!realized)
            {
                const bool rollbackDeviceLost =
                    classifyDeviceOperation(generation.deviceResources, requiredRollbackResult)
                        == GraphicsOperationResult::DeviceLost;
                generationLost = generationLost
                    || classifyDeviceOperation(generation.deviceResources, realizationFailure)
                        == GraphicsOperationResult::DeviceLost;
                const HRESULT cleanupResult = releasePresentationTargetResources(
                    generation.deviceResources, candidate, !generationLost);
                const bool requiredCleanupFailed = FAILED(requiredRollbackResult)
                    || FAILED(cleanupResult);
                generationLost = generationLost
                    || classifyDeviceOperation(generation.deviceResources, cleanupResult)
                        == GraphicsOperationResult::DeviceLost;
                if (rollbackDeviceLost)
                {
                    // Rollback is a shared DeviceLost event, but this candidate
                    // never became a logical target or acquired local authority.
                    impl_->transitionToPendingRecovery();
                    return {};
                }
                if (!generationLost && target.transport
                        == PresentationTransport::DirectCompositionSwapChain
                    && requiredCleanupFailed)
                {
                    const HRESULT failedRequiredOperation = FAILED(requiredRollbackResult)
                        ? requiredRollbackResult : cleanupResult;
                    (void)applyRequiredCompositionDisconnectOutcome(
                        generation.deviceResources, failedRequiredOperation,
                        hasOtherLogicalCompositionTarget(impl_->targetDescriptors, id));
                }
                if (!generationLost)
                    return {};
            }
        }
    }

    impl_->targetDescriptors.push_back(descriptor);
    if (realized)
        impl_->currentGeneration->targets.push_back(std::move(candidate));

    if (impl_->nextPresentationTargetId
        == (std::numeric_limits<std::uint64_t>::max)())
    {
        impl_->targetIdExhausted = true;
    }
    else
    {
        ++impl_->nextPresentationTargetId;
    }

    if (!impl_->preferredPacingTargetId.isValid())
        impl_->preferredPacingTargetId = id;
    if (generationLost)
        impl_->transitionToPendingRecovery();
    return id;
}

bool GraphicsBackend::detachPresentationTarget(PresentationTargetId id) noexcept
{
    if (!impl_ || !impl_->initialized || !id.isValid())
    {
        return false;
    }

    const auto found = std::find_if(impl_->targetDescriptors.begin(),
        impl_->targetDescriptors.end(),
        [id](const TargetDescriptor& descriptor) { return descriptor.id == id; });
    if (found == impl_->targetDescriptors.end())
        return false;

    const bool wasCompositionTarget =
        found->transport == PresentationTransport::DirectCompositionSwapChain;
    const bool hasSurvivingCompositionTarget = hasOtherLogicalCompositionTarget(
        impl_->targetDescriptors, id);
    const bool detachedPreferredTarget = found->id == impl_->preferredPacingTargetId;
    bool generationLost = false;
    if (impl_->forceDeviceLostOnDetachIdForTesting == id)
    {
        impl_->forceDeviceLostOnDetachIdForTesting = {};
        generationLost = true;
    }
    if (impl_->currentGeneration)
    {
        auto& generation = *impl_->currentGeneration;
        generationLost = generationLost || isDeviceLost(generation.deviceResources);
        HRESULT requiredTransactionResult = S_OK;
        const auto realized = std::find_if(generation.targets.begin(), generation.targets.end(),
            [id](const TargetRealization& target) { return target.id == id; });
        if (realized != generation.targets.end())
        {
            requiredTransactionResult = releasePresentationTargetResources(
                generation.deviceResources, *realized, !generationLost
                    && generation.deviceResources.compositionTrustState
                        == CompositionTrustState::Trusted);
            generation.targets.erase(realized);
        }
        if (isDeviceLost(generation.deviceResources))
            generationLost = true;
        if (wasCompositionTarget && !generationLost)
            generationLost = applyRequiredCompositionDisconnectOutcome(
                generation.deviceResources, requiredTransactionResult,
                hasSurvivingCompositionTarget);
    }
    impl_->targetDescriptors.erase(found);
    if (detachedPreferredTarget)
        impl_->preferredPacingTargetId = {};
    if (generationLost && impl_->currentGeneration)
        impl_->transitionToPendingRecovery();
    else if (wasCompositionTarget && !hasSurvivingCompositionTarget
        && impl_->currentGeneration)
    {
        // The final logical DComp target ends this capability's lifetime even
        // when its disconnect transaction failed. D3D and media stay alive.
        auto& resources = impl_->currentGeneration->deviceResources;
        resources.compositionDevice.Reset();
        resources.compositionTrustState = CompositionTrustState::Trusted;
    }
    return true;
}

bool GraphicsBackend::setPresentationPacingTarget(
    PresentationTargetId id) noexcept
{
    if (!impl_ || !impl_->initialized || !id.isValid())
    {
        return false;
    }

    if (!impl_->findDescriptor(id))
        return false;
    if (const auto* target = impl_->findRealization(id);
        target && target->state == PresentationTargetState::Failed)
        return false;

    impl_->preferredPacingTargetId = id;
    return true;
}

Detail::FramePreparationResult GraphicsBackend::beginFrame()
{
    if (!impl_ || !impl_->initialized)
    {
        return Detail::FramePreparationResult::GraphicsUnavailable;
    }
    if (!impl_->currentGeneration)
        return Detail::FramePreparationResult::GraphicsUnavailable;
    auto& generation = *impl_->currentGeneration;
    if (isDeviceLost(generation.deviceResources))
    {
        impl_->transitionToPendingRecovery();
        return Detail::FramePreparationResult::DeviceLost;
    }

    const bool attemptedCompositionRecovery =
        generation.deviceResources.compositionTrustState == CompositionTrustState::UntrustedPending
        && hasOtherLogicalCompositionTarget(impl_->targetDescriptors, {});
    if (attemptedCompositionRecovery
        && recoverCompositionDomain(generation, impl_->targetDescriptors)
            == Detail::GraphicsOperationResult::DeviceLost)
    {
        impl_->transitionToPendingRecovery();
        return Detail::FramePreparationResult::DeviceLost;
    }

    for (std::size_t index = 0; index < generation.targets.size(); ++index)
    {
        auto& target = generation.targets[index];
        const auto& descriptor = impl_->targetDescriptors[index];
        if (descriptor.transport == PresentationTransport::DirectCompositionSwapChain
            && (attemptedCompositionRecovery
                || generation.deviceResources.compositionTrustState
                    != CompositionTrustState::Trusted))
            continue;
        const bool wasOccluded = target.state == PresentationTargetState::Occluded;
        bool reconciliationLost = false;
        HRESULT requiredDisconnectResult = S_OK;
        (void)reconcilePresentationTarget(generation.deviceResources, descriptor, target,
            reconciliationLost, requiredDisconnectResult);
        if (descriptor.transport == PresentationTransport::DirectCompositionSwapChain
            && FAILED(requiredDisconnectResult) && !reconciliationLost)
            reconciliationLost = applyRequiredCompositionDisconnectOutcome(
                generation.deviceResources, requiredDisconnectResult,
                hasOtherLogicalCompositionTarget(impl_->targetDescriptors, descriptor.id));
        if (reconciliationLost || isDeviceLost(generation.deviceResources))
        {
            impl_->transitionToPendingRecovery();
            return Detail::FramePreparationResult::DeviceLost;
        }

        if (wasOccluded && target.state == PresentationTargetState::Occluded
            && target.swapChain
            && descriptor.transport == PresentationTransport::HwndSwapChain)
        {
            const PresentationResult probe = presentPresentationTarget(
                generation.deviceResources,
                target,
                0,
                DXGI_PRESENT_TEST);
            const Detail::PresentResultCategory category =
                Detail::classifyPresentResult(
                static_cast<std::int32_t>(probe.result),
                static_cast<std::int32_t>(probe.deviceRemovedReason));
            target.state = Detail::presentationTargetStateAfterPresent(
                target.state,
                category,
                target.renderTargetView && hasDrawableViewport(descriptor, target));
            switch (category)
            {
            case Detail::PresentResultCategory::Presented:
            case Detail::PresentResultCategory::Occluded:
            case Detail::PresentResultCategory::BusySkipped:
                break;
            case Detail::PresentResultCategory::TargetFailed:
                generation.deviceResources.context->OMSetRenderTargets(
                    0, nullptr, nullptr);
                target.renderTargetView.Reset();
                if (IsWindow(reinterpret_cast<HWND>(descriptor.nativeHandle))
                    && !isDeviceLost(generation.deviceResources))
                    target.pendingTargetLocalRecovery = true;
                break;
            case Detail::PresentResultCategory::DeviceLost:
                impl_->transitionToPendingRecovery();
                return Detail::FramePreparationResult::DeviceLost;
            }
        }
    }

    return std::any_of(
        generation.targets.begin(),
        generation.targets.end(),
        [&resources = generation.deviceResources](const TargetRealization& target)
        {
            return isTargetEffectivelyReady(resources, target);
        })
        ? Detail::FramePreparationResult::ReadyTargetsAvailable
        : Detail::FramePreparationResult::NoReadyTargets;
}

Detail::GenerationReplacementResult GraphicsBackend::recoverGraphicsGeneration(
    const Detail::MediaView* media)
{
    using Detail::GenerationReplacementResult;
    using Detail::MediaUpdateResult;
    if (!impl_ || !impl_->initialized)
        return GenerationReplacementResult::GenerationUnavailable;
    if (impl_->currentGeneration)
        return GenerationReplacementResult::GenerationAvailable;
    if (!std::exchange(impl_->pendingGenerationRecovery, false))
        return GenerationReplacementResult::GenerationUnavailable;

    ++impl_->graphicsRecoveryAttemptCountForTesting;
    if (std::exchange(impl_->failNextGenerationCreationForTesting, false))
        return GenerationReplacementResult::GenerationUnavailable;

    std::unique_ptr<GraphicsGeneration> candidate;
    try
    {
        candidate = std::make_unique<GraphicsGeneration>();
        candidate->targets.reserve(impl_->targetDescriptors.size());
    }
    catch (const std::bad_alloc&)
    {
        return GenerationReplacementResult::GenerationUnavailable;
    }

    if (!createDeviceResources(candidate->deviceResources))
        return GenerationReplacementResult::GenerationUnavailable;
    candidate->deviceResources.generation = impl_->nextDeviceGenerationForTesting++;
    ++impl_->graphicsGenerationCreationCountForTesting;

    for (const auto& descriptor : impl_->targetDescriptors)
    {
        TargetRealization realization{};
        const auto forcedResult =
            impl_->consumeForcedTargetRealizationResultForTesting(descriptor.id);

        HRESULT realizationFailure = S_OK;
        const bool realized = !forcedResult
            && createPresentationTargetResources(
                candidate->deviceResources, descriptor, realization, true,
                &realizationFailure);
        if (!realized)
        {
            int observedClientWidth = realization.observedClientWidth;
            int observedClientHeight = realization.observedClientHeight;
            if (forcedResult)
            {
                const auto clientSize =
                    Platform::Windows::queryNativeWindowClientSize(
                        descriptor.nativeHandle);
                if (clientSize.has_value())
                {
                    observedClientWidth = clientSize->width;
                    observedClientHeight = clientSize->height;
                }
            }

            bool candidateLost =
                (forcedResult
                    && *forcedResult == Detail::GraphicsOperationResult::DeviceLost)
                || classifyDeviceOperation(candidate->deviceResources, realizationFailure)
                    == GraphicsOperationResult::DeviceLost;
            const HRESULT cleanupResult = releasePresentationTargetResources(
                candidate->deviceResources, realization, !candidateLost);
            realization.observedClientWidth = observedClientWidth;
            realization.observedClientHeight = observedClientHeight;
            candidateLost = candidateLost
                || classifyDeviceOperation(candidate->deviceResources, cleanupResult)
                    == GraphicsOperationResult::DeviceLost;
            realization.id = descriptor.id;
            if (descriptor.transport == PresentationTransport::DirectCompositionSwapChain)
                realization.transport.emplace<CompositionSwapChainResources>();
            if (candidateLost)
            {
                impl_->discardGeneration(std::move(candidate), false);
                return GenerationReplacementResult::GenerationUnavailable;
            }
            const bool endpointValid =
                IsWindow(reinterpret_cast<HWND>(descriptor.nativeHandle)) != FALSE;
            const bool zeroNativeSize = endpointValid
                && (observedClientWidth == 0 || observedClientHeight == 0);
            realization.state = zeroNativeSize
                ? PresentationTargetState::Suspended : PresentationTargetState::Failed;
            if (endpointValid && !zeroNativeSize
                && (!forcedResult
                    || *forcedResult == Detail::GraphicsOperationResult::ResourceFailure)
                && (descriptor.transport == PresentationTransport::HwndSwapChain
                    || candidate->deviceResources.compositionTrustState
                        == CompositionTrustState::Trusted))
                realization.pendingTargetLocalRecovery = true;
        }
        candidate->targets.push_back(std::move(realization));
    }

    if (media)
    {
        const auto forcedMediaResult =
            impl_->consumeForcedMediaUpdateResultForTesting();
        const MediaUpdateResult mediaResult = forcedMediaResult
            ? *forcedMediaResult
            : createMediaRealization(
                candidate->deviceResources, candidate->currentMedia, *media);
        if (mediaResult == MediaUpdateResult::DeviceLost)
        {
            impl_->discardGeneration(std::move(candidate), false);
            return GenerationReplacementResult::GenerationUnavailable;
        }
        // ResourceFailure is media-local: the healthy candidate publishes
        // without a CurrentMediaRealization and without another recovery edge.
    }

    impl_->currentGeneration = std::move(candidate);
    return GenerationReplacementResult::GenerationAvailable;
}

Detail::MediaUpdateResult GraphicsBackend::updateImage(
    const void* imageData,
    int width,
    int height,
    int channels,
    std::size_t rowStride,
    std::size_t byteSize
)
{
    using Detail::MediaUpdateResult;
    if (!impl_ || !impl_->initialized)
        return MediaUpdateResult::GraphicsUnavailable;
    if (imageData == nullptr
        || !hasValidRgbaBuffer(width, height, channels, rowStride, byteSize))
        return MediaUpdateResult::ResourceFailure;
    if (!impl_->currentGeneration)
        return MediaUpdateResult::GraphicsUnavailable;
    if (const auto forced = impl_->consumeForcedMediaUpdateResultForTesting())
    {
        if (*forced == MediaUpdateResult::DeviceLost)
            impl_->transitionToPendingRecovery();
        return *forced;
    }

    auto& generation = *impl_->currentGeneration;
    const Detail::MediaView view{
        imageData, width, height, channels, rowStride, byteSize};
    const MediaUpdateResult result = createMediaRealization(
        generation.deviceResources, generation.currentMedia, view);
    if (result == MediaUpdateResult::DeviceLost)
        impl_->transitionToPendingRecovery();
    return result;
}

Detail::MediaUpdateResult GraphicsBackend::updateTexture(
    const void* frameData,
    int width,
    int height,
    int channels,
    std::size_t rowStride,
    std::size_t byteSize
)
{
    using Detail::GraphicsOperationResult;
    using Detail::MediaUpdateResult;
    if (!impl_ || !impl_->initialized)
        return MediaUpdateResult::GraphicsUnavailable;
    if (frameData == nullptr
        || !hasValidRgbaBuffer(width, height, channels, rowStride, byteSize))
        return MediaUpdateResult::ResourceFailure;
    if (!impl_->currentGeneration)
        return MediaUpdateResult::GraphicsUnavailable;
    if (const auto forced = impl_->consumeForcedMediaUpdateResultForTesting())
    {
        if (*forced == MediaUpdateResult::DeviceLost)
            impl_->transitionToPendingRecovery();
        return *forced;
    }

    auto& generation = *impl_->currentGeneration;
    auto& media = generation.currentMedia;
    if (media.texture
        && media.shaderResourceView
        && media.width == width
        && media.height == height
        && media.channels == channels)
    {
        ++generation.deviceResources.updateSubresourceCalls;
        if (generation.deviceResources.updateSubresourceCalls == 1
            || generation.deviceResources.updateSubresourceCalls % 25 == 0)
        {
            std::fprintf(
                stderr,
                "[GraphicsBackend] UpdateSubresource=%llu size=%dx%d stride=%zu bytes=%zu\n",
                static_cast<unsigned long long>(
                    generation.deviceResources.updateSubresourceCalls),
                width,
                height,
                rowStride,
                byteSize);
            std::fflush(stderr);
        }

        generation.deviceResources.context->UpdateSubresource(
            media.texture.Get(),
            0,
            nullptr,
            frameData,
            static_cast<UINT>(rowStride),
            0
        );
        const HRESULT removedReason =
            generation.deviceResources.device->GetDeviceRemovedReason();
        if (Detail::classifyGraphicsOperationResult(
                static_cast<std::int32_t>(S_OK),
                static_cast<std::int32_t>(removedReason))
            == GraphicsOperationResult::DeviceLost)
        {
            impl_->transitionToPendingRecovery();
            return MediaUpdateResult::DeviceLost;
        }
        ++media.uploadCountForTesting;
        return MediaUpdateResult::Updated;
    }

    const Detail::MediaView view{
        frameData, width, height, channels, rowStride, byteSize};
    const MediaUpdateResult result = createMediaRealization(
        generation.deviceResources, media, view);
    if (result == MediaUpdateResult::DeviceLost)
        impl_->transitionToPendingRecovery();
    return result;
}

void GraphicsBackend::clearImage() noexcept
{
    if (!impl_)
    {
        return;
    }

    if (!impl_->currentGeneration)
        return;
    auto& media = impl_->currentGeneration->currentMedia;
    media.shaderResourceView.Reset();
    media.texture.Reset();
    media.width = 0;
    media.height = 0;
    media.channels = 0;
}

void GraphicsBackend::renderPrepared(const RenderData& data)
{
    if (!impl_ || !impl_->initialized || !impl_->currentGeneration)
        return;

    auto& generation = *impl_->currentGeneration;
    if (isDeviceLost(generation.deviceResources))
    {
        impl_->transitionToPendingRecovery();
        return;
    }
    bool generationLost = false;
    const bool canDrawImage = updatePresentationConstants(
        generation.deviceResources,
        generation.currentMedia,
        data.presentation);
    // Clear also writes final framebuffer pixels, including uncovered regions.
    const float opaqueClearColor[4] = {
        data.clearColor[0], data.clearColor[1], data.clearColor[2], 1.0F};
    const auto submitTarget = [this, &generation, &generationLost, &data,
        &opaqueClearColor, canDrawImage](
        const TargetDescriptor& descriptor,
        TargetRealization& target,
        UINT syncInterval,
        UINT flags)
    {
        bool transparentOutside = false;
        if (data.canvasOutside && data.canvasOutside->targetId == descriptor.id)
        {
            const auto mode = data.canvasOutside->mode;
            if ((mode != CanvasOutsideMode::Opaque && mode != CanvasOutsideMode::Transparent)
                || (mode == CanvasOutsideMode::Transparent
                    && descriptor.transport != PresentationTransport::DirectCompositionSwapChain))
                return;
            transparentOutside = mode == CanvasOutsideMode::Transparent;
        }
        if (generationLost
            || !bindPresentationTarget(generation.deviceResources, descriptor, target))
        {
            return;
        }

        constexpr float transparentClearColor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        generation.deviceResources.context->ClearRenderTargetView(
            target.renderTargetView.Get(),
            transparentOutside ? transparentClearColor : opaqueClearColor);
        // ClearRenderTargetView clears the full RTV regardless of viewport.
        // Only the draw mapping is target-dependent; media constants stay shared.
        const auto viewport = derivePresentationViewport(
            target.realizedClientWidth, viewportHeight(descriptor, target), data.canvasAspect);
        if (viewport && canDrawImage)
        {
            setViewport(generation.deviceResources, *viewport);
            drawSharedImage(generation.deviceResources, generation.currentMedia);
            ++target.drawCountForTesting;
        }

        captureRenderedPixel(generation.deviceResources, descriptor, target);

        ++target.presentAttemptCountForTesting;
        target.lastPresentSyncIntervalForTesting = syncInterval;
        target.lastPresentFlagsForTesting = flags;
        target.lastPresentOrderForTesting = ++impl_->presentOrder;
        const PresentationResult presentationResult = presentPresentationTarget(
            generation.deviceResources,
            target,
            syncInterval,
            flags);
        const Detail::PresentResultCategory category = Detail::classifyPresentResult(
            static_cast<std::int32_t>(presentationResult.result),
            static_cast<std::int32_t>(presentationResult.deviceRemovedReason));
        target.state = Detail::presentationTargetStateAfterPresent(
            target.state,
            category,
            target.renderTargetView && hasDrawableViewport(descriptor, target));
        switch (category)
        {
        case Detail::PresentResultCategory::Presented:
        case Detail::PresentResultCategory::BusySkipped:
            break;
        case Detail::PresentResultCategory::Occluded:
            break;
        case Detail::PresentResultCategory::TargetFailed:
            generation.deviceResources.context->OMSetRenderTargets(
                0, nullptr, nullptr);
            target.renderTargetView.Reset();
            if (IsWindow(reinterpret_cast<HWND>(descriptor.nativeHandle))
                && !isDeviceLost(generation.deviceResources))
                target.pendingTargetLocalRecovery = true;
            std::fprintf(
                stderr,
                "[GraphicsBackend] target Present failed: HRESULT=0x%08lX\n",
                static_cast<unsigned long>(presentationResult.result));
            std::fflush(stderr);
            break;
        case Detail::PresentResultCategory::DeviceLost:
            generationLost = true;
            break;
        }
    };

    const PresentationTargetId pacingTargetId = impl_->usablePacingTargetId();
    for (std::size_t index = 0; index < generation.targets.size(); ++index)
    {
        auto& target = generation.targets[index];
        if (isTargetEffectivelyReady(generation.deviceResources, target)
            && target.id != pacingTargetId)
        {
            submitTarget(impl_->targetDescriptors[index], target,
                0U, DXGI_PRESENT_DO_NOT_WAIT);
            if (generationLost)
            {
                impl_->transitionToPendingRecovery();
                return;
            }
        }
    }

    const auto pacing = std::find_if(generation.targets.begin(), generation.targets.end(),
        [pacingTargetId, &resources = generation.deviceResources](const TargetRealization& target)
        { return target.id == pacingTargetId && isTargetEffectivelyReady(resources, target); });
    if (pacing != generation.targets.end())
    {
        const auto index = static_cast<std::size_t>(pacing - generation.targets.begin());
        submitTarget(impl_->targetDescriptors[index], *pacing, 1U, 0U);
    }
    if (generationLost)
        impl_->transitionToPendingRecovery();
}

void GraphicsBackend::shutdown() noexcept
{
    if (!impl_)
    {
        return;
    }

    impl_->initialized = false;
    const bool generationLost = impl_->currentGeneration
        && isDeviceLost(impl_->currentGeneration->deviceResources);
    impl_->retireGeneration(!generationLost);
    impl_->targetDescriptors.clear();
    impl_->preferredPacingTargetId = {};
    impl_->presentOrder = 0U;
    impl_->pendingGenerationRecovery = false;
}

}
