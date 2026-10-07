#include "ImageResource.h"

#include <Windows.h>

#include <wincodec.h>
#include <wrl/client.h>

#include <limits>
#include <new>
#include <utility>

namespace
{

using Microsoft::WRL::ComPtr;

class ComInitialization
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

}

namespace DesktopSceneEngine::Resources
{

MediaType ImageResource::type() const noexcept
{
    return MediaType::Image;
}

bool ImageResource::loadFromFile(const std::filesystem::path& path)
{
    pixels_.clear();
    width_ = 0;
    height_ = 0;
    channels_ = 0;
    rowStride_ = 0;

    const ComInitialization comInitialization{};
    if (!comInitialization.valid())
    {
        return false;
    }

    try
    {
        ComPtr<IWICImagingFactory> factory{};
        if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))))
        {
            return false;
        }

        ComPtr<IWICBitmapDecoder> decoder{};
        if (FAILED(factory->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder)))
        {
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame{};
        if (FAILED(decoder->GetFrame(0, &frame)))
        {
            return false;
        }

        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        if (FAILED(frame->GetSize(&sourceWidth, &sourceHeight))
            || sourceWidth == 0
            || sourceHeight == 0
            || sourceWidth > static_cast<UINT>((std::numeric_limits<int>::max)())
            || sourceHeight > static_cast<UINT>((std::numeric_limits<int>::max)()))
        {
            return false;
        }

        ComPtr<IWICFormatConverter> converter{};
        if (FAILED(factory->CreateFormatConverter(&converter))
            || FAILED(converter->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom)))
        {
            return false;
        }

        constexpr std::size_t channels = 4;
        const std::size_t pixelCount =
            static_cast<std::size_t>(sourceWidth)
            * static_cast<std::size_t>(sourceHeight);
        if (pixelCount > (std::numeric_limits<std::size_t>::max)() / channels)
        {
            return false;
        }

        const std::size_t decodedSize = pixelCount * channels;
        const std::size_t strideSize =
            static_cast<std::size_t>(sourceWidth) * channels;
        if (strideSize > (std::numeric_limits<UINT>::max)()
            || decodedSize > (std::numeric_limits<UINT>::max)())
        {
            return false;
        }

        std::vector<unsigned char> decodedPixels(decodedSize);
        const UINT stride = static_cast<UINT>(strideSize);
        const UINT bufferSize = static_cast<UINT>(decodedSize);
        if (FAILED(converter->CopyPixels(
            nullptr,
            stride,
            bufferSize,
            decodedPixels.data())))
        {
            return false;
        }

        pixels_ = std::move(decodedPixels);
        width_ = static_cast<int>(sourceWidth);
        height_ = static_cast<int>(sourceHeight);
        channels_ = static_cast<int>(channels);
        rowStride_ = strideSize;
        return true;
    }
    catch (const std::bad_alloc&)
    {
        pixels_.clear();
        width_ = 0;
        height_ = 0;
        channels_ = 0;
        rowStride_ = 0;
        return false;
    }
}

const unsigned char* ImageResource::data() const noexcept
{
    return pixels_.empty() ? nullptr : pixels_.data();
}

int ImageResource::width() const noexcept
{
    return width_;
}

int ImageResource::height() const noexcept
{
    return height_;
}

int ImageResource::channels() const noexcept
{
    return channels_;
}

std::size_t ImageResource::rowStride() const noexcept
{
    return rowStride_;
}

std::size_t ImageResource::byteSize() const noexcept
{
    return pixels_.size();
}

}
