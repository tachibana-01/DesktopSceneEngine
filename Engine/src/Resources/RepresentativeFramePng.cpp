#include "RepresentativeFramePng.h"

#include <Windows.h>

#include <wincodec.h>
#include <wrl/client.h>

#include <chrono>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace
{

using Microsoft::WRL::ComPtr;

constexpr std::size_t RgbaBytesPerPixel = 4U;

class ComInitialization final
{
public:

    ComInitialization()
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized_ = result == S_OK || result == S_FALSE;
        valid_ = SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
    }

    ~ComInitialization()
    {
        if (initialized_)
        {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return valid_;
    }

private:

    bool initialized_ = false;
    bool valid_ = false;
};

class TemporaryFileCleanup final
{
public:

    explicit TemporaryFileCleanup(std::filesystem::path path)
        : path_{std::move(path)}
    {
    }

    ~TemporaryFileCleanup()
    {
        if (!active_)
        {
            return;
        }

        std::error_code error{};
        std::filesystem::remove(path_, error);
    }

    void release() noexcept
    {
        active_ = false;
    }

private:

    std::filesystem::path path_{};
    bool active_ = true;
};

bool hasValidFrameStorage(
    const DesktopSceneEngine::Resources::RepresentativeFrameRgba& frame,
    std::size_t& requiredByteCount
) noexcept
{
    if (frame.width <= 0 || frame.height <= 0)
    {
        return false;
    }

    const std::size_t width = static_cast<std::size_t>(frame.width);
    if (width > (std::numeric_limits<std::size_t>::max)() / RgbaBytesPerPixel)
    {
        return false;
    }

    const std::size_t minimumRowStride = width * RgbaBytesPerPixel;
    if (frame.rowStride < minimumRowStride)
    {
        return false;
    }

    const std::size_t height = static_cast<std::size_t>(frame.height);
    if (height > (std::numeric_limits<std::size_t>::max)() / frame.rowStride)
    {
        return false;
    }

    requiredByteCount = frame.rowStride * height;
    return frame.rgba.size() >= requiredByteCount
        && frame.rowStride <= (std::numeric_limits<UINT>::max)()
        && requiredByteCount <= (std::numeric_limits<UINT>::max)();
}

bool hasValidDecodedDimensions(UINT width, UINT height, std::size_t& rowStride, std::size_t& byteCount) noexcept
{
    if (width == 0U || height == 0U
        || width > static_cast<UINT>((std::numeric_limits<int>::max)())
        || height > static_cast<UINT>((std::numeric_limits<int>::max)()))
    {
        return false;
    }

    const std::size_t widthSize = static_cast<std::size_t>(width);
    if (widthSize > (std::numeric_limits<std::size_t>::max)() / RgbaBytesPerPixel)
    {
        return false;
    }

    rowStride = widthSize * RgbaBytesPerPixel;
    const std::size_t heightSize = static_cast<std::size_t>(height);
    if (heightSize > (std::numeric_limits<std::size_t>::max)() / rowStride)
    {
        return false;
    }

    byteCount = rowStride * heightSize;
    return rowStride <= (std::numeric_limits<UINT>::max)()
        && byteCount <= (std::numeric_limits<UINT>::max)();
}

DesktopSceneEngine::Resources::RepresentativeFramePngStatus encodePng(
    const std::filesystem::path& path,
    const DesktopSceneEngine::Resources::RepresentativeFrameRgba& frame,
    std::size_t byteCount
) noexcept
{
    using DesktopSceneEngine::Resources::RepresentativeFramePngStatus;

    const ComInitialization comInitialization{};
    if (!comInitialization.valid())
    {
        return RepresentativeFramePngStatus::ComInitializationFailed;
    }

    ComPtr<IWICImagingFactory> factory{};
    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))))
    {
        return RepresentativeFramePngStatus::ComInitializationFailed;
    }

    ComPtr<IWICStream> stream{};
    if (FAILED(factory->CreateStream(&stream))
        || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
    {
        return RepresentativeFramePngStatus::FileOpenFailed;
    }

    ComPtr<IWICBitmapEncoder> encoder{};
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
    {
        return RepresentativeFramePngStatus::EncodeFailed;
    }

    ComPtr<IWICBitmap> sourceBitmap{};
    if (FAILED(factory->CreateBitmapFromMemory(
            static_cast<UINT>(frame.width),
            static_cast<UINT>(frame.height),
            GUID_WICPixelFormat32bppRGBA,
            static_cast<UINT>(frame.rowStride),
            static_cast<UINT>(byteCount),
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(frame.rgba.data())),
            &sourceBitmap)))
    {
        return RepresentativeFramePngStatus::EncodeFailed;
    }

    ComPtr<IWICBitmapFrameEncode> frameEncoder{};
    ComPtr<IPropertyBag2> options{};
    if (FAILED(encoder->CreateNewFrame(&frameEncoder, &options))
        || FAILED(frameEncoder->Initialize(options.Get()))
        || FAILED(frameEncoder->SetSize(
            static_cast<UINT>(frame.width),
            static_cast<UINT>(frame.height))))
    {
        return RepresentativeFramePngStatus::EncodeFailed;
    }

    GUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    BOOL canConvert = FALSE;
    ComPtr<IWICFormatConverter> converter{};
    if (FAILED(frameEncoder->SetPixelFormat(&pixelFormat))
        || (!IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppRGBA)
            && !IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA))
        || FAILED(factory->CreateFormatConverter(&converter))
        || FAILED(converter->CanConvert(
            GUID_WICPixelFormat32bppRGBA,
            pixelFormat,
            &canConvert))
        || canConvert == FALSE
        || FAILED(converter->Initialize(
            sourceBitmap.Get(),
            pixelFormat,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom))
        || FAILED(frameEncoder->WriteSource(converter.Get(), nullptr))
        || FAILED(frameEncoder->Commit())
        || FAILED(encoder->Commit()))
    {
        return RepresentativeFramePngStatus::EncodeFailed;
    }

    return RepresentativeFramePngStatus::Written;
}

std::optional<std::filesystem::path> makeTemporaryPath(
    const std::filesystem::path& finalPath
) noexcept
{
    try
    {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::wstring baseName = finalPath.filename().wstring() + L".tmp."
            + std::to_wstring(ticks);

        for (std::uint32_t suffix = 0U; suffix < 1024U; ++suffix)
        {
            const std::filesystem::path candidate = finalPath.parent_path()
                / (baseName + L"." + std::to_wstring(suffix));
            std::error_code error{};
            const bool alreadyExists = std::filesystem::exists(candidate, error);
            if (error)
            {
                return std::nullopt;
            }

            if (!alreadyExists)
            {
                return candidate;
            }
        }
    }
    catch (const std::bad_alloc&)
    {
    }

    return std::nullopt;
}

bool finalizeTemporaryPng(
    const std::filesystem::path& temporaryPath,
    const std::filesystem::path& finalPath,
    bool finalPathExists
) noexcept
{
    if (finalPathExists)
    {
        return ReplaceFileW(
            finalPath.c_str(),
            temporaryPath.c_str(),
            nullptr,
            REPLACEFILE_WRITE_THROUGH,
            nullptr,
            nullptr)
            != FALSE;
    }

    return MoveFileExW(
        temporaryPath.c_str(),
        finalPath.c_str(),
        MOVEFILE_WRITE_THROUGH)
        != FALSE;
}

}

namespace DesktopSceneEngine::Resources
{

RepresentativeFramePngStatus writeRepresentativeFramePng(
    const std::filesystem::path& finalPath,
    const RepresentativeFrameRgba& frame
) noexcept
{
    std::size_t byteCount = 0U;
    if (!hasValidFrameStorage(frame, byteCount) || finalPath.empty())
    {
        return RepresentativeFramePngStatus::InvalidFrame;
    }

    try
    {
        std::error_code error{};
        const bool finalPathExists = std::filesystem::exists(finalPath, error);
        if (error)
        {
            return RepresentativeFramePngStatus::FileOpenFailed;
        }

        if (finalPathExists)
        {
            const RepresentativeFramePngReadResult existing = readRepresentativeFramePng(finalPath);
            if (existing.status == RepresentativeFramePngStatus::Read)
            {
                return RepresentativeFramePngStatus::ReusedExisting;
            }

            if (existing.status == RepresentativeFramePngStatus::ComInitializationFailed
                || existing.status == RepresentativeFramePngStatus::FileOpenFailed)
            {
                return existing.status;
            }
        }

        const std::filesystem::path parent = finalPath.parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent, error);
            if (error)
            {
                return RepresentativeFramePngStatus::FileOpenFailed;
            }
        }

        const auto temporaryPath = makeTemporaryPath(finalPath);
        if (!temporaryPath.has_value())
        {
            return RepresentativeFramePngStatus::FileOpenFailed;
        }

        TemporaryFileCleanup temporaryCleanup{*temporaryPath};
        const RepresentativeFramePngStatus encodeStatus = encodePng(*temporaryPath, frame, byteCount);
        if (encodeStatus != RepresentativeFramePngStatus::Written)
        {
            return encodeStatus;
        }

        if (!finalizeTemporaryPng(*temporaryPath, finalPath, finalPathExists))
        {
            return RepresentativeFramePngStatus::FinalizeFailed;
        }

        temporaryCleanup.release();
        return RepresentativeFramePngStatus::Written;
    }
    catch (const std::bad_alloc&)
    {
        return RepresentativeFramePngStatus::FileOpenFailed;
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return RepresentativeFramePngStatus::FileOpenFailed;
    }
}

RepresentativeFramePngReadResult readRepresentativeFramePng(
    const std::filesystem::path& path
) noexcept
{
    RepresentativeFramePngReadResult result{};

    try
    {
        std::error_code error{};
        const bool exists = std::filesystem::exists(path, error);
        if (error)
        {
            result.status = RepresentativeFramePngStatus::FileOpenFailed;
            return result;
        }

        if (!exists)
        {
            result.status = RepresentativeFramePngStatus::Missing;
            return result;
        }

        const ComInitialization comInitialization{};
        if (!comInitialization.valid())
        {
            result.status = RepresentativeFramePngStatus::ComInitializationFailed;
            return result;
        }

        ComPtr<IWICImagingFactory> factory{};
        if (FAILED(CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&factory))))
        {
            result.status = RepresentativeFramePngStatus::ComInitializationFailed;
            return result;
        }

        ComPtr<IWICBitmapDecoder> decoder{};
        if (FAILED(factory->CreateDecoderFromFilename(
                path.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnLoad,
                &decoder)))
        {
            result.status = RepresentativeFramePngStatus::DecodeFailed;
            return result;
        }

        ComPtr<IWICBitmapFrameDecode> sourceFrame{};
        if (FAILED(decoder->GetFrame(0U, &sourceFrame)))
        {
            result.status = RepresentativeFramePngStatus::DecodeFailed;
            return result;
        }

        UINT width = 0U;
        UINT height = 0U;
        std::size_t rowStride = 0U;
        std::size_t byteCount = 0U;
        if (FAILED(sourceFrame->GetSize(&width, &height))
            || !hasValidDecodedDimensions(width, height, rowStride, byteCount))
        {
            result.status = RepresentativeFramePngStatus::DecodeFailed;
            return result;
        }

        ComPtr<IWICFormatConverter> converter{};
        if (FAILED(factory->CreateFormatConverter(&converter))
            || FAILED(converter->Initialize(
                sourceFrame.Get(),
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom)))
        {
            result.status = RepresentativeFramePngStatus::DecodeFailed;
            return result;
        }

        RepresentativeFrameRgba decoded{};
        decoded.rgba.resize(byteCount);
        if (FAILED(converter->CopyPixels(
                nullptr,
                static_cast<UINT>(rowStride),
                static_cast<UINT>(byteCount),
                decoded.rgba.data())))
        {
            return result;
        }

        decoded.width = static_cast<int>(width);
        decoded.height = static_cast<int>(height);
        decoded.rowStride = rowStride;
        result.status = RepresentativeFramePngStatus::Read;
        result.frame = std::move(decoded);
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return result;
    }
    catch (const std::filesystem::filesystem_error&)
    {
        result.status = RepresentativeFramePngStatus::FileOpenFailed;
        return result;
    }
}

}
