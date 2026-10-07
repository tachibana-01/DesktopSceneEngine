#include "RepresentativeFrameStore.h"

#include <DesktopSceneEngine/Resources/PreviewVideoDecoder.h>

#include "RepresentativeFrameSampling.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace DesktopSceneEngine::Resources
{

namespace
{

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

[[nodiscard]] bool cancellationRequested(const std::atomic_bool* cancellation) noexcept
{
    return cancellation != nullptr && cancellation->load(std::memory_order_acquire);
}

[[nodiscard]] std::optional<std::filesystem::path> makeTemporaryPath(
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
            if (!std::filesystem::exists(candidate, error) && !error)
            {
                return candidate;
            }

            if (error)
            {
                return std::nullopt;
            }
        }
    }
    catch (const std::bad_alloc&)
    {
    }

    return std::nullopt;
}

[[nodiscard]] bool writeTextAtomically(
    const std::filesystem::path& finalPath,
    const std::string& text
) noexcept
{
    if (finalPath.empty())
    {
        return false;
    }

    try
    {
        std::error_code error{};
        const std::filesystem::path parent = finalPath.parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent, error);
            if (error)
            {
                return false;
            }
        }

        const bool finalExists = std::filesystem::exists(finalPath, error);
        if (error)
        {
            return false;
        }

        const auto temporaryPath = makeTemporaryPath(finalPath);
        if (!temporaryPath.has_value())
        {
            return false;
        }

        TemporaryFileCleanup temporaryCleanup{*temporaryPath};
        {
            std::ofstream output{*temporaryPath, std::ios::binary | std::ios::trunc};
            if (!output.is_open())
            {
                return false;
            }

            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.flush();
            output.close();
            if (!output.good())
            {
                return false;
            }
        }

        const BOOL finalized = finalExists
            ? ReplaceFileW(
                finalPath.c_str(),
                temporaryPath->c_str(),
                nullptr,
                REPLACEFILE_WRITE_THROUGH,
                nullptr,
                nullptr)
            : MoveFileExW(
                temporaryPath->c_str(),
                finalPath.c_str(),
                MOVEFILE_WRITE_THROUGH);
        if (finalized == FALSE)
        {
            return false;
        }

        temporaryCleanup.release();
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return false;
    }
}

[[nodiscard]] std::optional<RepresentativeFrameCacheMetadata> readReusableMetadata(
    const RepresentativeFrameCacheLayout& layout,
    const SourceIdentity& source
) noexcept
{
    try
    {
        std::ifstream input{layout.metadataPath(), std::ios::binary};
        if (!input.is_open())
        {
            return std::nullopt;
        }

        const std::string serialized{
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
        const auto metadata = deserializeRepresentativeFrameCacheMetadata(serialized);
        if (!metadata.has_value() || !isRepresentativeFrameCacheReusable(*metadata, source))
        {
            return std::nullopt;
        }

        return metadata;
    }
    catch (const std::bad_alloc&)
    {
        return std::nullopt;
    }
}

[[nodiscard]] RepresentativeFrameCacheMetadata makeMetadata(
    const SourceIdentity& source,
    const RepresentativeFrameRgba& frame,
    const std::optional<std::int64_t>& durationMilliseconds,
    const std::optional<RepresentativeFrameCacheMetadata>& existing
)
{
    RepresentativeFrameCacheMetadata metadata = existing.value_or(RepresentativeFrameCacheMetadata{});
    metadata.cacheFormatVersion = CacheFormatVersion;
    metadata.samplingPolicyVersion = SamplingPolicyVersion;
    metadata.sourceIdentity = source;
    metadata.frameDimensions = RepresentativeFrameDimensions{frame.width, frame.height};
    if (durationMilliseconds.has_value() && *durationMilliseconds >= 0)
    {
        metadata.durationMilliseconds = *durationMilliseconds;
    }

    const RepresentativeFrameSamplingPlan samplingPlan = makeRepresentativeFrameSamplingPlan(
        metadata.durationMilliseconds.value_or(0));
    metadata.samplingIntervalMilliseconds = samplingPlan.intervalMilliseconds;
    return metadata;
}

void addGeneratedTimestamp(
    RepresentativeFrameCacheMetadata& metadata,
    std::int64_t timestampMilliseconds
)
{
    const auto existing = std::lower_bound(
        metadata.generatedTimestampsMilliseconds.begin(),
        metadata.generatedTimestampsMilliseconds.end(),
        timestampMilliseconds);
    if (existing == metadata.generatedTimestampsMilliseconds.end()
        || *existing != timestampMilliseconds)
    {
        metadata.generatedTimestampsMilliseconds.insert(existing, timestampMilliseconds);
    }
}

[[nodiscard]] bool revalidateSource(
    const std::filesystem::path& sourcePath,
    const SourceIdentity& expected
) noexcept
{
    const auto current = sourceIdentityForFile(sourcePath);
    return current.has_value() && *current == expected;
}

[[nodiscard]] bool hasValidFrame(const RepresentativeFrameRgba& frame) noexcept
{
    if (frame.width <= 0 || frame.height <= 0)
    {
        return false;
    }

    const std::size_t width = static_cast<std::size_t>(frame.width);
    if (width > (std::numeric_limits<std::size_t>::max)() / 4U)
    {
        return false;
    }

    if (frame.rowStride < width * 4U)
    {
        return false;
    }

    const std::size_t height = static_cast<std::size_t>(frame.height);
    return height <= (std::numeric_limits<std::size_t>::max)() / frame.rowStride
        && frame.rgba.size() >= frame.rowStride * height;
}

[[nodiscard]] std::optional<std::int64_t> durationMilliseconds(float seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds <= 0.0F)
    {
        return std::nullopt;
    }

    const double milliseconds = static_cast<double>(seconds) * 1000.0;
    if (milliseconds > static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
    {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(std::llround(milliseconds));
}

}

struct RepresentativeFrameStore::Impl
{
    explicit Impl(
        std::filesystem::path root,
        std::size_t byteBudget,
        RepresentativeFrameGenerationCallback callback
    )
        : mediaCacheRoot{std::move(root)}
        , hotCache{byteBudget}
        , generator{std::move(callback)}
    {
    }

    [[nodiscard]] RepresentativeFrameGenerationResult generate(
        const SourceIdentity& source,
        std::int64_t timestampMilliseconds,
        const std::atomic_bool* cancellation
    ) noexcept
    {
        if (cancellationRequested(cancellation))
        {
            return {RepresentativeFrameGenerationStatus::Cancelled, {}, std::nullopt};
        }

        if (generator)
        {
            try
            {
                return generator(source, timestampMilliseconds, cancellation);
            }
            catch (...)
            {
                return {};
            }
        }

        if (!openedSource.has_value() || *openedSource != source)
        {
            previewDecoder.clear();
            if (!previewDecoder.open(source.normalizedPath))
            {
                return {};
            }

            openedSource = source;
        }

        constexpr PreviewDecodeOptions RepresentativeFrameSize{320, 320};
        const double timestampSeconds = static_cast<double>(timestampMilliseconds) / 1000.0;
        if (timestampSeconds > static_cast<double>((std::numeric_limits<float>::max)()))
        {
            return {};
        }

        PreviewDecodeResult decoded = previewDecoder.frameAt(
            static_cast<float>(timestampSeconds),
            RepresentativeFrameSize,
            cancellation);
        if (decoded.status == PreviewDecodeStatus::Cancelled)
        {
            return {RepresentativeFrameGenerationStatus::Cancelled, {}, std::nullopt};
        }
        if (decoded.status != PreviewDecodeStatus::Ready)
        {
            return {};
        }

        RepresentativeFrameRgba frame{
            std::move(decoded.frame.rgba),
            decoded.frame.width,
            decoded.frame.height,
            decoded.frame.rowStride};
        if (!hasValidFrame(frame) || cancellationRequested(cancellation))
        {
            return cancellationRequested(cancellation)
                ? RepresentativeFrameGenerationResult{
                    RepresentativeFrameGenerationStatus::Cancelled,
                    {},
                    std::nullopt}
                : RepresentativeFrameGenerationResult{};
        }

        return {
            RepresentativeFrameGenerationStatus::Ready,
            std::move(frame),
            durationMilliseconds(previewDecoder.duration())};
    }

    std::filesystem::path mediaCacheRoot{};
    RepresentativeFrameMemoryCache hotCache;
    RepresentativeFrameGenerationCallback generator{};
    PreviewVideoDecoder previewDecoder{};
    std::optional<SourceIdentity> openedSource{};
};

RepresentativeFrameStore::RepresentativeFrameStore(
    std::filesystem::path mediaCacheRoot,
    std::size_t hotCacheByteBudget
)
    : impl_{std::make_unique<Impl>(
        std::move(mediaCacheRoot),
        hotCacheByteBudget,
        RepresentativeFrameGenerationCallback{})}
{
}

RepresentativeFrameStore::RepresentativeFrameStore(
    std::filesystem::path mediaCacheRoot,
    std::size_t hotCacheByteBudget,
    RepresentativeFrameGenerationCallback generator
)
    : impl_{std::make_unique<Impl>(
        std::move(mediaCacheRoot),
        hotCacheByteBudget,
        std::move(generator))}
{
}

RepresentativeFrameStore::~RepresentativeFrameStore() = default;

RepresentativeFrameStore::RepresentativeFrameStore(RepresentativeFrameStore&&) noexcept = default;

RepresentativeFrameStore& RepresentativeFrameStore::operator=(RepresentativeFrameStore&&) noexcept = default;

RepresentativeFrameStoreResult RepresentativeFrameStore::loadOrGenerate(
    const std::filesystem::path& sourcePath,
    std::int64_t timestampMilliseconds,
    const std::atomic_bool* cancellation
) noexcept
{
    RepresentativeFrameStoreResult result{};
    try
    {
        if (!impl_ || impl_->mediaCacheRoot.empty() || timestampMilliseconds < 0
            || cancellationRequested(cancellation))
        {
            result.status = cancellationRequested(cancellation)
                ? RepresentativeFrameStoreStatus::Cancelled
                : RepresentativeFrameStoreStatus::InvalidRequest;
            return result;
        }

        const auto source = sourceIdentityForFile(sourcePath);
        if (!source.has_value())
        {
            return result;
        }

        const SourceFingerprint fingerprint = makeSourceFingerprint(*source);
        const RepresentativeFrameCacheKey key{fingerprint, timestampMilliseconds};
        if (const auto memoryFrame = impl_->hotCache.find(key); memoryFrame.has_value())
        {
            result.status = RepresentativeFrameStoreStatus::RamHit;
            result.frame = std::move(*memoryFrame);
            return result;
        }

        const RepresentativeFrameCacheLayout layout{impl_->mediaCacheRoot, fingerprint};
        const std::filesystem::path framePath = layout.framePath(
            static_cast<std::uint64_t>(timestampMilliseconds));
        const RepresentativeFramePngReadResult diskFrame = readRepresentativeFramePng(framePath);
        if (diskFrame.status == RepresentativeFramePngStatus::Read && hasValidFrame(diskFrame.frame))
        {
            result.status = RepresentativeFrameStoreStatus::DiskHit;
            result.frame = diskFrame.frame;
            if (!revalidateSource(sourcePath, *source))
            {
                result.status = RepresentativeFrameStoreStatus::SourceChanged;
                result.frame = {};
                return result;
            }

            const auto existingMetadata = readReusableMetadata(layout, *source);
            if (!existingMetadata.has_value()
                || !std::binary_search(
                    existingMetadata->generatedTimestampsMilliseconds.begin(),
                    existingMetadata->generatedTimestampsMilliseconds.end(),
                    timestampMilliseconds))
            {
                RepresentativeFrameCacheMetadata metadata = makeMetadata(
                    *source,
                    result.frame,
                    std::nullopt,
                    existingMetadata);
                addGeneratedTimestamp(metadata, timestampMilliseconds);
                result.persistenceFailed = !writeTextAtomically(
                    layout.metadataPath(),
                    serializeRepresentativeFrameCacheMetadata(metadata));
            }

            (void)impl_->hotCache.insert(key, result.frame);
            return result;
        }

        if (cancellationRequested(cancellation))
        {
            result.status = RepresentativeFrameStoreStatus::Cancelled;
            return result;
        }

        RepresentativeFrameGenerationResult generated = impl_->generate(
            *source,
            timestampMilliseconds,
            cancellation);
        if (generated.status == RepresentativeFrameGenerationStatus::Cancelled)
        {
            result.status = RepresentativeFrameStoreStatus::Cancelled;
            return result;
        }
        if (generated.status != RepresentativeFrameGenerationStatus::Ready || !hasValidFrame(generated.frame))
        {
            result.status = RepresentativeFrameStoreStatus::DecodeUnavailable;
            return result;
        }
        if (cancellationRequested(cancellation))
        {
            result.status = RepresentativeFrameStoreStatus::Cancelled;
            return result;
        }
        if (!revalidateSource(sourcePath, *source))
        {
            result.status = RepresentativeFrameStoreStatus::SourceChanged;
            return result;
        }

        result.status = RepresentativeFrameStoreStatus::Generated;
        result.frame = std::move(generated.frame);
        const RepresentativeFramePngStatus pngStatus = writeRepresentativeFramePng(framePath, result.frame);
        const bool pngPersisted = pngStatus == RepresentativeFramePngStatus::Written
            || pngStatus == RepresentativeFramePngStatus::ReusedExisting;
        if (!pngPersisted)
        {
            result.persistenceFailed = true;
            (void)impl_->hotCache.insert(key, result.frame);
            return result;
        }

        if (!revalidateSource(sourcePath, *source))
        {
            result.status = RepresentativeFrameStoreStatus::SourceChanged;
            result.frame = {};
            return result;
        }

        const auto existingMetadata = readReusableMetadata(layout, *source);
        RepresentativeFrameCacheMetadata metadata = makeMetadata(
            *source,
            result.frame,
            generated.durationMilliseconds,
            existingMetadata);
        addGeneratedTimestamp(metadata, timestampMilliseconds);
        result.persistenceFailed = !writeTextAtomically(
            layout.metadataPath(),
            serializeRepresentativeFrameCacheMetadata(metadata));
        (void)impl_->hotCache.insert(key, result.frame);
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return result;
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return result;
    }
}

std::size_t RepresentativeFrameStore::hotCacheByteSize() const noexcept
{
    return impl_ ? impl_->hotCache.byteSize() : 0U;
}

std::size_t RepresentativeFrameStore::hotCacheFrameCount() const noexcept
{
    return impl_ ? impl_->hotCache.frameCount() : 0U;
}

}
