#include "TimelinePreviewProvider.h"

#include "TimelineRepresentativeTimestamp.h"

#include "Resources/RepresentativeFrameStore.h"

#include <QImageReader>

#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>

namespace DesktopSceneEngine::Player
{

namespace
{

template<typename Frame>
bool hasValidRgbaStorage(const Frame& frame) noexcept
{
    if (frame.width <= 0 || frame.height <= 0)
    {
        return false;
    }

    const std::size_t width = static_cast<std::size_t>(frame.width);
    const std::size_t height = static_cast<std::size_t>(frame.height);
    if (width > (std::numeric_limits<std::size_t>::max)() / 4U)
    {
        return false;
    }

    const std::size_t minimumRowStride = width * 4U;
    if (frame.rowStride < minimumRowStride
        || frame.rowStride > (std::numeric_limits<std::size_t>::max)() / height)
    {
        return false;
    }

    return frame.rgba.size() >= frame.rowStride * height;
}

}

TimelinePreviewProvider::TimelinePreviewProvider() = default;

TimelinePreviewProvider::TimelinePreviewProvider(std::filesystem::path mediaCacheRoot)
    : representativeFrameStore_{mediaCacheRoot.empty()
        ? nullptr
        : std::make_unique<Resources::RepresentativeFrameStore>(std::move(mediaCacheRoot))}
{
}

TimelinePreviewProvider::~TimelinePreviewProvider() = default;

bool TimelinePreviewProvider::setSource(
    ItemId itemId,
    const QString& source,
    WorkspaceMediaType mediaType
)
{
    if (hasSource() && itemId_ == itemId && source_ == source && mediaType_ == mediaType)
    {
        return true;
    }

    clear();
    if (source.trimmed().isEmpty())
    {
        return false;
    }

    if (mediaType == WorkspaceMediaType::Image)
    {
        QImageReader reader{source};
        const QImage loaded = reader.read();
        if (loaded.isNull())
        {
            return false;
        }

        itemId_ = itemId;
        source_ = source;
        mediaType_ = mediaType;
        image_ = loaded.copy();
        return true;
    }

    if (mediaType == WorkspaceMediaType::Video && representativeFrameStore_)
    {
        itemId_ = itemId;
        source_ = source;
        mediaType_ = mediaType;
        return true;
    }

    return false;
}

void TimelinePreviewProvider::clear() noexcept
{
    image_ = QImage{};
    itemId_ = ItemId{};
    source_.clear();
    mediaType_.reset();
}

QImage TimelinePreviewProvider::frameAt(float)
{
    if (!mediaType_.has_value())
    {
        return {};
    }

    if (*mediaType_ == WorkspaceMediaType::Image)
    {
        return image_.isNull() ? QImage{} : image_.copy();
    }

    return {};
}

TimelinePreviewVideoFrameResult TimelinePreviewProvider::videoFrameAt(
    float timeSeconds,
    std::int64_t durationMilliseconds,
    const std::atomic_bool* cancellationRequested
)
{
    TimelinePreviewVideoFrameResult result{};
    if (!mediaType_.has_value() || *mediaType_ != WorkspaceMediaType::Video
        || !representativeFrameStore_ || !std::isfinite(timeSeconds))
    {
        return result;
    }

    const double hoverMilliseconds = static_cast<double>(timeSeconds) * 1'000.0;
    if (hoverMilliseconds < static_cast<double>((std::numeric_limits<std::int64_t>::min)())
        || hoverMilliseconds > static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
    {
        return result;
    }

    const auto representativeTimestamp = representativeTimestampForTimelineHover(
        durationMilliseconds,
        static_cast<std::int64_t>(std::llround(hoverMilliseconds)));
    if (!representativeTimestamp.has_value())
    {
        return result;
    }

    Resources::RepresentativeFrameStoreResult stored = representativeFrameStore_->loadOrGenerate(
        std::filesystem::path{source_.toStdWString()},
        *representativeTimestamp,
        cancellationRequested);
    if (stored.status == Resources::RepresentativeFrameStoreStatus::Cancelled)
    {
        result.cancelled = true;
        return result;
    }

    switch (stored.status)
    {
    case Resources::RepresentativeFrameStoreStatus::RamHit:
        result.storeOutcome = TimelinePreviewStoreOutcome::RamHit;
        break;
    case Resources::RepresentativeFrameStoreStatus::DiskHit:
        result.storeOutcome = TimelinePreviewStoreOutcome::DiskHit;
        break;
    case Resources::RepresentativeFrameStoreStatus::Generated:
        result.storeOutcome = TimelinePreviewStoreOutcome::Generated;
        break;
    default:
        break;
    }

    if (!hasValidRgbaStorage(stored.frame))
    {
        return result;
    }

    const QImage borrowed{
        stored.frame.rgba.data(),
        stored.frame.width,
        stored.frame.height,
        static_cast<qsizetype>(stored.frame.rowStride),
        QImage::Format_RGBA8888};
    result.image = borrowed.copy();
    return result;
}

bool TimelinePreviewProvider::hasSource() const noexcept
{
    if (!mediaType_.has_value())
    {
        return false;
    }

    return *mediaType_ == WorkspaceMediaType::Image
        ? !image_.isNull()
        : representativeFrameStore_ != nullptr && !source_.isEmpty();
}

}
