#include "DefaultAudioEndpointWatcher.h"

#include <atomic>
#include <new>

#if defined(_WIN32)

#include <Windows.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#endif

namespace DesktopSceneEngine::Platform::Audio
{

#if defined(_WIN32)

namespace
{

class EndpointNotificationClient final : public IMMNotificationClient
{
public:
    explicit EndpointNotificationClient(std::atomic<std::uint64_t>* generation) noexcept
        : generation_(generation)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override
    {
        if (object == nullptr)
        {
            return E_POINTER;
        }

        *object = nullptr;
        if (interfaceId == IID_IUnknown || interfaceId == __uuidof(IMMNotificationClient))
        {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return referenceCount_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = referenceCount_.fetch_sub(1U, std::memory_order_acq_rel) - 1U;
        if (remaining == 0U)
        {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(
        LPCWSTR,
        DWORD
    ) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow flow,
        ERole role,
        LPCWSTR
    ) override
    {
        if (flow == eRender && role == eConsole && generation_ != nullptr)
        {
            generation_->fetch_add(1U, std::memory_order_relaxed);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR,
        const PROPERTYKEY
    ) override
    {
        return S_OK;
    }

private:
    std::atomic<ULONG> referenceCount_{1U};
    std::atomic<std::uint64_t>* generation_ = nullptr;
};

}

#endif

struct DefaultAudioEndpointWatcher::Impl
{
    std::atomic<std::uint64_t> generation{0U};
#if defined(_WIN32)
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator{};
    Microsoft::WRL::ComPtr<EndpointNotificationClient> callback{};
    bool comInitialized = false;
    bool registered = false;
    bool registrationReferenceHeld = false;
#endif
};

DefaultAudioEndpointWatcher::DefaultAudioEndpointWatcher()
    : impl_(std::make_unique<Impl>())
{
}

DefaultAudioEndpointWatcher::~DefaultAudioEndpointWatcher()
{
    shutdown();
}

bool DefaultAudioEndpointWatcher::initialize() noexcept
{
    if (impl_ == nullptr)
    {
        return false;
    }

#if defined(_WIN32)
    if (impl_->registered)
    {
        return true;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (comResult == S_OK || comResult == S_FALSE)
    {
        impl_->comInitialized = true;
    }
    else if (comResult != RPC_E_CHANGED_MODE)
    {
        return false;
    }

    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&impl_->enumerator))))
    {
        shutdown();
        return false;
    }

    EndpointNotificationClient* const callback = new (std::nothrow)
        EndpointNotificationClient(&impl_->generation);
    if (callback == nullptr)
    {
        shutdown();
        return false;
    }
    impl_->callback.Attach(callback);

    if (FAILED(impl_->enumerator->RegisterEndpointNotificationCallback(callback)))
    {
        shutdown();
        return false;
    }

    // MMDevice does not retain the callback. Keep an explicit registration
    // reference until after unregister has completed.
    callback->AddRef();
    impl_->registrationReferenceHeld = true;
    impl_->registered = true;
    return true;
#else
    return false;
#endif
}

void DefaultAudioEndpointWatcher::shutdown() noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }

#if defined(_WIN32)
    if (impl_->registered && impl_->enumerator != nullptr && impl_->callback != nullptr)
    {
        // The callback remains strongly held until this synchronous attempt
        // returns. A failed unregister is diagnostic-only in this V1 watcher.
        (void)impl_->enumerator->UnregisterEndpointNotificationCallback(impl_->callback.Get());
    }
    impl_->registered = false;

    if (impl_->registrationReferenceHeld && impl_->callback != nullptr)
    {
        impl_->callback->Release();
        impl_->registrationReferenceHeld = false;
    }
    impl_->callback.Reset();
    impl_->enumerator.Reset();

    if (impl_->comInitialized)
    {
        CoUninitialize();
        impl_->comInitialized = false;
    }
#endif
}

std::uint64_t DefaultAudioEndpointWatcher::generation() const noexcept
{
    return impl_ == nullptr
        ? 0U
        : impl_->generation.load(std::memory_order_relaxed);
}

void DefaultAudioEndpointWatcher::recordDefaultDeviceChangedForTesting(
    std::uint32_t flow,
    std::uint32_t role,
    const wchar_t* deviceId
) noexcept
{
#if defined(_WIN32)
    EndpointNotificationClient callback{&impl_->generation};
    (void)callback.OnDefaultDeviceChanged(
        static_cast<EDataFlow>(flow),
        static_cast<ERole>(role),
        deviceId);
#else
    static_cast<void>(flow);
    static_cast<void>(role);
    static_cast<void>(deviceId);
#endif
}

void DefaultAudioEndpointWatcher::recordOtherCallbacksForTesting() noexcept
{
#if defined(_WIN32)
    EndpointNotificationClient callback{&impl_->generation};
    const PROPERTYKEY key{};
    (void)callback.OnDeviceStateChanged(L"device", 0U);
    (void)callback.OnDeviceAdded(L"device");
    (void)callback.OnDeviceRemoved(L"device");
    (void)callback.OnPropertyValueChanged(L"device", key);
#endif
}

bool DefaultAudioEndpointWatcher::verifyCallbackComContractForTesting() noexcept
{
#if defined(_WIN32)
    std::atomic<std::uint64_t> generation{0U};
    EndpointNotificationClient* const callback = new (std::nothrow)
        EndpointNotificationClient(&generation);
    if (callback == nullptr)
    {
        return false;
    }

    void* unknown = nullptr;
    const HRESULT unknownResult = callback->QueryInterface(IID_IUnknown, &unknown);
    if (SUCCEEDED(unknownResult) && unknown != nullptr)
    {
        static_cast<IUnknown*>(unknown)->Release();
    }

    void* notification = nullptr;
    const HRESULT notificationResult = callback->QueryInterface(
        __uuidof(IMMNotificationClient),
        &notification);
    if (SUCCEEDED(notificationResult) && notification != nullptr)
    {
        static_cast<IMMNotificationClient*>(notification)->Release();
    }

    constexpr GUID unsupportedInterface{
        0x5be7a6d1,
        0x6735,
        0x4b05,
        {0x82, 0xb2, 0x30, 0x19, 0x0c, 0x08, 0x57, 0x72}};
    void* unsupported = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1U));
    const HRESULT unsupportedResult = callback->QueryInterface(
        unsupportedInterface,
        &unsupported);

    callback->Release();
    return unknownResult == S_OK
        && notificationResult == S_OK
        && unsupportedResult == E_NOINTERFACE
        && unsupported == nullptr;
#else
    return true;
#endif
}

}
