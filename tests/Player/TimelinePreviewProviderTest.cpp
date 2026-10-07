#include "TimelinePreviewProvider.h"

#include <QApplication>

#include <chrono>
#include <filesystem>
#include <iostream>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

class TemporaryDirectory final
{
public:

    TemporaryDirectory()
        : path_{std::filesystem::temp_directory_path()
            / ("DesktopSceneEngine-TimelinePreviewProviderTest-"
                + std::to_string(std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()))}
    {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:

    std::filesystem::path path_{};
};

bool containsPersistentPng(const std::filesystem::path& root)
{
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator{root, error}, end;
         !error && iterator != end;
         iterator.increment(error))
    {
        if (iterator->is_regular_file(error) && iterator->path().extension() == ".png")
        {
            return true;
        }
    }

    return false;
}

std::filesystem::path findFixture(const char* extension)
{
    const std::filesystem::path root{DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root})
    {
        if (entry.is_regular_file() && entry.path().extension() == extension)
        {
            return entry.path();
        }
    }

    return {};
}

bool verifyImageProvider(const std::filesystem::path& imagePath)
{
    using DesktopSceneEngine::Player::ItemId;
    using DesktopSceneEngine::Player::TimelinePreviewProvider;
    using DesktopSceneEngine::Player::TimelinePreviewStoreOutcome;
    using DesktopSceneEngine::Player::WorkspaceMediaType;

    TimelinePreviewProvider provider{};
    const ItemId imageId{QUuid::createUuid()};
    if (!require(provider.setSource(
            imageId,
            QString::fromStdWString(imagePath.native()),
            WorkspaceMediaType::Image), "Image source should load")
        || !require(provider.hasSource(), "Loaded image source should be active"))
    {
        return false;
    }

    const QImage atStart = provider.frameAt(0.0F);
    const QImage atFiveSeconds = provider.frameAt(5.0F);
    const QImage later = provider.frameAt(999.0F);
    const QImage snapshot = atStart.copy();
    if (!require(!atStart.isNull() && !atFiveSeconds.isNull() && !later.isNull(),
            "Static image previews should be available at any timeline time")
        || !require(atStart == atFiveSeconds && atStart == later,
            "Static image preview content should not vary with time")
        || !require(provider.setSource(
            imageId,
            QString::fromStdWString(imagePath.native()),
            WorkspaceMediaType::Image),
            "Setting the same image identity should safely reuse its source"))
    {
        return false;
    }

    provider.clear();
    return require(!provider.hasSource(), "Clearing should reset the active source")
        && require(atStart == snapshot,
            "Returned image QImage must remain unchanged after provider reset")
        && require(provider.frameAt(0.0F).isNull(),
            "Frame requests without a source should safely fail");
}

bool verifyVideoProvider(
    const std::filesystem::path& videoPath
)
{
    using DesktopSceneEngine::Player::ItemId;
    using DesktopSceneEngine::Player::TimelinePreviewProvider;
    using DesktopSceneEngine::Player::TimelinePreviewStoreOutcome;
    using DesktopSceneEngine::Player::WorkspaceMediaType;

    TemporaryDirectory cache{};
    QImage first{};
    QImage second{};
    {
        TimelinePreviewProvider provider{cache.path()};
        if (!require(provider.setSource(
                ItemId{QUuid::createUuid()},
                QString::fromStdWString(videoPath.native()),
                WorkspaceMediaType::Video), "Store-backed video source should configure"))
        {
            return false;
        }

        const auto firstResult = provider.videoFrameAt(12.0F, 60'000, nullptr);
        const auto sameBucketResult = provider.videoFrameAt(14.0F, 60'000, nullptr);
        if (!require(!firstResult.cancelled && !sameBucketResult.cancelled,
                "Store-backed video requests should not cancel without a cancellation token")
            || !require(!firstResult.image.isNull() && !sameBucketResult.image.isNull(),
                "Store-backed video requests should produce representative images")
            || !require(firstResult.image == sameBucketResult.image,
                "Hover times in one representative bucket should return the same image")
            || !require(firstResult.storeOutcome == TimelinePreviewStoreOutcome::Generated
                    && sameBucketResult.storeOutcome == TimelinePreviewStoreOutcome::RamHit,
                "Provider results should expose generated and RAM-hit Store outcomes")
            || !require(containsPersistentPng(cache.path()),
                "The first Store-backed request should persist a representative PNG"))
        {
            return false;
        }

        first = firstResult.image.copy();
    }

    TimelinePreviewProvider restartedProvider{cache.path()};
    if (!require(restartedProvider.setSource(
            ItemId{QUuid::createUuid()},
            QString::fromStdWString(videoPath.native()),
            WorkspaceMediaType::Video), "A restarted provider should configure the same source"))
    {
        return false;
    }
    const auto restartedResult = restartedProvider.videoFrameAt(12.0F, 60'000, nullptr);
    second = restartedResult.image;
    restartedProvider.clear();
    return require(!first.isNull() && !second.isNull(),
            "Video representative requests should return owned QImages")
        && require(first.width() <= 320 && first.height() <= 320,
            "Video preview frames should use the bounded preview decode size")
        && require(first == second,
            "A fresh provider should reuse the persisted representative frame")
        && require(restartedResult.storeOutcome == TimelinePreviewStoreOutcome::DiskHit,
            "A restarted provider should expose a disk-hit Store outcome")
        && require(!restartedProvider.hasSource(),
            "Clearing should release only the current provider source state")
        && require(restartedProvider.frameAt(0.0F).isNull(),
            "Frame requests after clearing should safely fail");
}

}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    const std::filesystem::path imagePath = findFixture(".png");
    const std::filesystem::path videoPath = findFixture(".mp4");
    if (!require(!imagePath.empty() && !videoPath.empty(),
            "Image and video fixtures must be available"))
    {
        return 1;
    }

    return verifyImageProvider(imagePath) && verifyVideoProvider(videoPath) ? 0 : 1;
}
