#pragma once

#include "WorkspaceModel.h"

#include <QImage>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace DesktopSceneEngine::Resources
{

class RepresentativeFrameStore;

}

namespace DesktopSceneEngine::Player
{

enum class TimelinePreviewStoreOutcome
{
    Unavailable,
    RamHit,
    DiskHit,
    Generated
};

struct TimelinePreviewVideoFrameResult
{
    QImage image{};
    bool cancelled = false;
    TimelinePreviewStoreOutcome storeOutcome = TimelinePreviewStoreOutcome::Unavailable;
};

// Player-local source ownership and Qt conversion for Timeline preview frames.
// Timeline geometry and playback navigation remain outside this class.
class TimelinePreviewProvider final
{
public:

    TimelinePreviewProvider();
    explicit TimelinePreviewProvider(std::filesystem::path mediaCacheRoot);
    ~TimelinePreviewProvider();

    TimelinePreviewProvider(const TimelinePreviewProvider&) = delete;
    TimelinePreviewProvider& operator=(const TimelinePreviewProvider&) = delete;
    TimelinePreviewProvider(TimelinePreviewProvider&&) = delete;
    TimelinePreviewProvider& operator=(TimelinePreviewProvider&&) = delete;

    [[nodiscard]] bool setSource(
        ItemId itemId,
        const QString& source,
        WorkspaceMediaType mediaType
    );

    void clear() noexcept;

    [[nodiscard]] QImage frameAt(float timeSeconds);

    [[nodiscard]] TimelinePreviewVideoFrameResult videoFrameAt(
        float timeSeconds,
        std::int64_t durationMilliseconds,
        const std::atomic_bool* cancellationRequested
    );

    [[nodiscard]] bool hasSource() const noexcept;

private:

    ItemId itemId_{};
    QString source_{};
    std::optional<WorkspaceMediaType> mediaType_{};
    QImage image_{};
    std::unique_ptr<Resources::RepresentativeFrameStore> representativeFrameStore_{};
};

}
