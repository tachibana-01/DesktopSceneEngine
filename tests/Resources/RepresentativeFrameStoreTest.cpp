#include "Resources/RepresentativeFrameMemoryCache.h"
#include "Resources/RepresentativeFrameStore.h"

#include "Resources/RepresentativeFrameCache.h"
#include "Resources/RepresentativeFramePng.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{

using DesktopSceneEngine::Resources::RepresentativeFrameCacheKey;
using DesktopSceneEngine::Resources::RepresentativeFrameCacheLayout;
using DesktopSceneEngine::Resources::RepresentativeFrameGenerationResult;
using DesktopSceneEngine::Resources::RepresentativeFrameGenerationStatus;
using DesktopSceneEngine::Resources::RepresentativeFrameMemoryCache;
using DesktopSceneEngine::Resources::RepresentativeFramePngStatus;
using DesktopSceneEngine::Resources::RepresentativeFrameRgba;
using DesktopSceneEngine::Resources::RepresentativeFrameStore;
using DesktopSceneEngine::Resources::RepresentativeFrameStoreResult;
using DesktopSceneEngine::Resources::RepresentativeFrameStoreStatus;
using DesktopSceneEngine::Resources::SourceFingerprint;

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
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("DesktopSceneEngine-RepresentativeFrameStoreTest-" + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:

    std::filesystem::path path_{};
};

RepresentativeFrameRgba makeFrame(
    int width,
    int height,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue
)
{
    RepresentativeFrameRgba frame{};
    frame.width = width;
    frame.height = height;
    frame.rowStride = static_cast<std::size_t>(width) * 4U;
    frame.rgba.resize(frame.rowStride * static_cast<std::size_t>(height));
    for (std::size_t offset = 0U; offset < frame.rgba.size(); offset += 4U)
    {
        frame.rgba[offset] = red;
        frame.rgba[offset + 1U] = green;
        frame.rgba[offset + 2U] = blue;
        frame.rgba[offset + 3U] = 255U;
    }

    return frame;
}

bool framesEqual(const RepresentativeFrameRgba& left, const RepresentativeFrameRgba& right)
{
    return left.width == right.width
        && left.height == right.height
        && left.rowStride == right.rowStride
        && left.rgba == right.rgba;
}

bool writeSourceFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    output.close();
    return output.good();
}

std::optional<DesktopSceneEngine::Resources::RepresentativeFrameCacheMetadata> readMetadata(
    const std::filesystem::path& path
)
{
    std::ifstream input{path, std::ios::binary};
    if (!input.is_open())
    {
        return std::nullopt;
    }

    const std::string serialized{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    return DesktopSceneEngine::Resources::deserializeRepresentativeFrameCacheMetadata(serialized);
}

bool hasTemporaryFiles(const std::filesystem::path& root)
{
    std::error_code error{};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root, error})
    {
        if (error)
        {
            return true;
        }

        if (entry.path().filename().string().find(".tmp.") != std::string::npos)
        {
            return true;
        }
    }

    return false;
}

RepresentativeFrameGenerationResult readyFrame(
    RepresentativeFrameRgba frame,
    std::int64_t durationMilliseconds = 20000
)
{
    return {
        RepresentativeFrameGenerationStatus::Ready,
        std::move(frame),
        durationMilliseconds};
}

bool verifyByteBoundedLru()
{
    RepresentativeFrameMemoryCache cache{8U};
    const SourceFingerprint fingerprint{"same-source"};
    const RepresentativeFrameCacheKey first{fingerprint, 0};
    const RepresentativeFrameCacheKey second{fingerprint, 5000};
    const RepresentativeFrameCacheKey third{fingerprint, 10000};
    const RepresentativeFrameRgba firstFrame = makeFrame(1, 1, 10U, 0U, 0U);
    const RepresentativeFrameRgba secondFrame = makeFrame(1, 1, 0U, 20U, 0U);
    const RepresentativeFrameRgba thirdFrame = makeFrame(1, 1, 0U, 0U, 30U);

    if (!require(cache.insert(first, firstFrame) && cache.insert(second, secondFrame),
            "two budget-sized frames must insert")
        || !require(cache.byteSize() == 8U && cache.frameCount() == 2U,
            "RAM cache byte accounting must use owned frame bytes")
        || !require(cache.find(first).has_value(),
            "a RAM hit must make the accessed frame most recent")
        || !require(cache.insert(third, thirdFrame),
            "inserting after a hit must remain within the byte budget"))
    {
        return false;
    }

    RepresentativeFrameRgba replacement = makeFrame(2, 1, 40U, 50U, 60U);
    RepresentativeFrameRgba oversized = makeFrame(3, 1, 70U, 80U, 90U);
    const bool result = require(!cache.find(second).has_value(),
            "LRU insertion must evict the least-recently-used frame")
        && require(cache.find(first).has_value() && cache.find(third).has_value(),
            "the accessed and newly inserted frames must remain hot")
        && require(cache.insert(first, replacement),
            "same-key replacement must succeed without double-counting")
        && require(cache.byteSize() == replacement.rgba.size() && cache.frameCount() == 1U,
            "same-key replacement must account for only its replacement bytes")
        && require(!cache.insert(third, oversized),
            "a single oversized frame must not enter the hot cache")
        && require(cache.byteSize() == replacement.rgba.size() && cache.frameCount() == 1U,
            "an oversized insertion must preserve existing cache accounting");
    cache.clear();
    return result
        && require(cache.byteSize() == 0U && cache.frameCount() == 0U,
            "clear must release all LRU state");
}

bool verifyRamDiskGenerateAndMetadata()
{
    TemporaryDirectory temporary{};
    const std::filesystem::path sourcePath = temporary.path() / "source.bin";
    const std::filesystem::path cacheRoot = temporary.path() / "media-cache";
    if (!require(writeSourceFile(sourcePath, "source-A"), "the temporary source must be writable"))
    {
        return false;
    }

    std::size_t generatorCalls = 0U;
    RepresentativeFrameStore store{
        cacheRoot,
        1024U,
        [&generatorCalls](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            ++generatorCalls;
            return readyFrame(makeFrame(2, 2, 1U, 2U, 3U));
        }};
    const RepresentativeFrameStoreResult first = store.loadOrGenerate(sourcePath, 5000);
    const RepresentativeFrameStoreResult second = store.loadOrGenerate(sourcePath, 5000);
    const auto source = DesktopSceneEngine::Resources::sourceIdentityForFile(sourcePath);
    if (!require(source.has_value(), "a source identity must exist after generation"))
    {
        return false;
    }

    const RepresentativeFrameCacheLayout layout{
        cacheRoot,
        DesktopSceneEngine::Resources::makeSourceFingerprint(*source)};
    const auto metadata = readMetadata(layout.metadataPath());
    const auto persisted = DesktopSceneEngine::Resources::readRepresentativeFramePng(layout.framePath(5000U));

    std::size_t diskGeneratorCalls = 0U;
    RepresentativeFrameStore diskStore{
        cacheRoot,
        1024U,
        [&diskGeneratorCalls](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            ++diskGeneratorCalls;
            return readyFrame(makeFrame(2, 2, 9U, 9U, 9U));
        }};
    const RepresentativeFrameStoreResult disk = diskStore.loadOrGenerate(sourcePath, 5000);
    const RepresentativeFrameStoreResult diskThenRam = diskStore.loadOrGenerate(sourcePath, 5000);

    return require(first.status == RepresentativeFrameStoreStatus::Generated
                && !first.persistenceFailed && !first.frame.rgba.empty(),
               "a RAM/disk miss must generate and return an owned frame")
        && require(second.status == RepresentativeFrameStoreStatus::RamHit && generatorCalls == 1U,
            "a repeated source/timestamp request must use RAM without regeneration")
        && require(std::filesystem::is_regular_file(layout.framePath(5000U)),
            "generation must persist the deterministic PNG path")
        && require(persisted.status == RepresentativeFramePngStatus::Read
                && framesEqual(first.frame, persisted.frame),
            "the persisted PNG must decode to the generated RGBA frame")
        && require(metadata.has_value()
                && metadata->generatedTimestampsMilliseconds == std::vector<std::int64_t>{5000},
            "metadata inventory must update only after the PNG persists")
        && require(disk.status == RepresentativeFrameStoreStatus::DiskHit
                && !disk.persistenceFailed && diskGeneratorCalls == 0U,
            "a new empty-RAM store must load the valid PNG without decoding")
        && require(diskThenRam.status == RepresentativeFrameStoreStatus::RamHit
                && diskGeneratorCalls == 0U,
            "a disk hit must populate RAM for the next identical request")
        && require(framesEqual(first.frame, disk.frame),
            "disk-hit frames must preserve the persisted RGBA representation")
        && require(!hasTemporaryFiles(cacheRoot),
            "persistent cache writes must leave no temporary artifacts");
}

bool verifyCorruptPngRecoveryAndMetadataRepair()
{
    TemporaryDirectory temporary{};
    const std::filesystem::path sourcePath = temporary.path() / "source.bin";
    const std::filesystem::path cacheRoot = temporary.path() / "cache";
    if (!require(writeSourceFile(sourcePath, "source-B"), "the temporary source must be writable"))
    {
        return false;
    }

    const auto source = DesktopSceneEngine::Resources::sourceIdentityForFile(sourcePath);
    if (!require(source.has_value(), "the source must have an identity"))
    {
        return false;
    }

    const RepresentativeFrameCacheLayout layout{
        cacheRoot,
        DesktopSceneEngine::Resources::makeSourceFingerprint(*source)};
    std::filesystem::create_directories(layout.framesDirectory());
    {
        std::ofstream corrupt{layout.framePath(10000U), std::ios::binary};
        corrupt << "corrupt PNG";
    }

    std::size_t recoveryCalls = 0U;
    RepresentativeFrameStore recoveryStore{
        cacheRoot,
        1024U,
        [&recoveryCalls](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            ++recoveryCalls;
            return readyFrame(makeFrame(2, 1, 4U, 5U, 6U));
        }};
    const RepresentativeFrameStoreResult recovered = recoveryStore.loadOrGenerate(sourcePath, 10000);
    const auto repaired = DesktopSceneEngine::Resources::readRepresentativeFramePng(layout.framePath(10000U));
    const auto repairedMetadata = readMetadata(layout.metadataPath());

    const RepresentativeFrameRgba directFrame = makeFrame(1, 2, 7U, 8U, 9U);
    if (!require(DesktopSceneEngine::Resources::writeRepresentativeFramePng(
            layout.framePath(15000U), directFrame) == RepresentativeFramePngStatus::Written,
            "a valid deterministic PNG can exist without metadata inventory"))
    {
        return false;
    }

    std::size_t repairCalls = 0U;
    RepresentativeFrameStore repairStore{
        cacheRoot,
        1024U,
        [&repairCalls](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            ++repairCalls;
            return readyFrame(makeFrame(1, 2, 1U, 1U, 1U));
        }};
    const RepresentativeFrameStoreResult diskRepair = repairStore.loadOrGenerate(sourcePath, 15000);
    const auto repairedInventory = readMetadata(layout.metadataPath());

    return require(recovered.status == RepresentativeFrameStoreStatus::Generated && recoveryCalls == 1U,
               "a corrupt deterministic PNG must fall through to source generation")
        && require(repaired.status == RepresentativeFramePngStatus::Read
                && framesEqual(recovered.frame, repaired.frame),
            "corrupt PNG recovery must safely replace the artifact")
        && require(repairedMetadata.has_value()
                && repairedMetadata->generatedTimestampsMilliseconds == std::vector<std::int64_t>{10000},
            "successful corrupt recovery must record only its successful timestamp")
        && require(diskRepair.status == RepresentativeFrameStoreStatus::DiskHit && repairCalls == 0U,
            "a valid unindexed PNG must be reusable without decoding")
        && require(repairedInventory.has_value()
                && repairedInventory->generatedTimestampsMilliseconds
                    == std::vector<std::int64_t>{10000, 15000},
            "a valid disk hit must repair the metadata inventory opportunistically");
}

bool verifySourceChangeCancellationAndPersistenceFailure()
{
    TemporaryDirectory temporary{};
    const std::filesystem::path sourcePath = temporary.path() / "source.bin";
    const std::filesystem::path cacheRoot = temporary.path() / "cache";
    if (!require(writeSourceFile(sourcePath, "source-C"), "the temporary source must be writable"))
    {
        return false;
    }

    const auto identityBeforeChange = DesktopSceneEngine::Resources::sourceIdentityForFile(sourcePath);
    if (!require(identityBeforeChange.has_value(), "the source must have an initial identity"))
    {
        return false;
    }

    const RepresentativeFrameCacheLayout changedLayout{
        cacheRoot,
        DesktopSceneEngine::Resources::makeSourceFingerprint(*identityBeforeChange)};
    RepresentativeFrameStore changingStore{
        cacheRoot,
        1024U,
        [&sourcePath](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            writeSourceFile(sourcePath, "source-C-mutated-during-generation");
            return readyFrame(makeFrame(1, 1, 10U, 11U, 12U));
        }};
    const RepresentativeFrameStoreResult changed = changingStore.loadOrGenerate(sourcePath, 0);

    std::atomic_bool cancellationFlag{true};
    std::size_t cancelledCalls = 0U;
    RepresentativeFrameStore cancelledStore{
        cacheRoot,
        1024U,
        [&cancelledCalls](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            ++cancelledCalls;
            return readyFrame(makeFrame(1, 1, 13U, 14U, 15U));
        }};
    const RepresentativeFrameStoreResult cancelledResult = cancelledStore.loadOrGenerate(
        sourcePath,
        5000,
        &cancellationFlag);

    const auto currentIdentity = DesktopSceneEngine::Resources::sourceIdentityForFile(sourcePath);
    if (!require(currentIdentity.has_value(), "the mutated source must retain an identity"))
    {
        return false;
    }

    const RepresentativeFrameCacheLayout failingLayout{
        cacheRoot,
        DesktopSceneEngine::Resources::makeSourceFingerprint(*currentIdentity)};
    std::filesystem::create_directories(failingLayout.metadataPath());
    std::size_t persistenceCalls = 0U;
    RepresentativeFrameStore persistenceStore{
        cacheRoot,
        1024U,
        [&persistenceCalls](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            ++persistenceCalls;
            return readyFrame(makeFrame(1, 1, 16U, 17U, 18U));
        }};
    const RepresentativeFrameStoreResult persistedWithoutMetadata = persistenceStore.loadOrGenerate(sourcePath, 0);
    const auto persisted = DesktopSceneEngine::Resources::readRepresentativeFramePng(failingLayout.framePath(0U));

    std::size_t diskFailureCalls = 0U;
    RepresentativeFrameStore diskAfterMetadataFailureStore{
        cacheRoot,
        1024U,
        [&diskFailureCalls](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            ++diskFailureCalls;
            return readyFrame(makeFrame(1, 1, 19U, 20U, 21U));
        }};
    const RepresentativeFrameStoreResult diskAfterMetadataFailure =
        diskAfterMetadataFailureStore.loadOrGenerate(sourcePath, 0);

    return require(changed.status == RepresentativeFrameStoreStatus::SourceChanged
                && !std::filesystem::exists(changedLayout.framePath(0U))
                && !std::filesystem::exists(changedLayout.metadataPath()),
               "a source mutation during generation must not poison the old fingerprint directory")
        && require(cancelledResult.status == RepresentativeFrameStoreStatus::Cancelled && cancelledCalls == 0U,
            "a pre-cancelled request must not invoke the generator or persist a frame")
        && require(persistedWithoutMetadata.status == RepresentativeFrameStoreStatus::Generated
                && persistedWithoutMetadata.persistenceFailed && persistenceCalls == 1U,
            "metadata failure after PNG write must still return the valid generated frame")
        && require(persisted.status == RepresentativeFramePngStatus::Read,
            "a valid PNG must survive a metadata inventory write failure")
        && require(diskAfterMetadataFailure.status == RepresentativeFrameStoreStatus::DiskHit
                && diskAfterMetadataFailure.persistenceFailed && diskFailureCalls == 0U,
            "a later disk hit must remain usable and report failed metadata repair");
}

bool verifyEmptyRootAndPngPersistenceFailure()
{
    TemporaryDirectory temporary{};
    const std::filesystem::path sourcePath = temporary.path() / "source.bin";
    if (!require(writeSourceFile(sourcePath, "source-D"), "the temporary source must be writable"))
    {
        return false;
    }

    std::size_t emptyRootCalls = 0U;
    RepresentativeFrameStore emptyRootStore{
        {},
        1024U,
        [&emptyRootCalls](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            ++emptyRootCalls;
            return readyFrame(makeFrame(1, 1, 22U, 23U, 24U));
        }};
    const RepresentativeFrameStoreResult emptyRoot = emptyRootStore.loadOrGenerate(sourcePath, 0);

    const std::filesystem::path blockedRoot = temporary.path() / "not-a-directory";
    if (!require(writeSourceFile(blockedRoot, "blocks cache directory creation"),
            "the blocked cache root fixture must be writable"))
    {
        return false;
    }

    std::size_t persistenceCalls = 0U;
    RepresentativeFrameStore persistenceStore{
        blockedRoot,
        1024U,
        [&persistenceCalls](
            const DesktopSceneEngine::Resources::SourceIdentity&,
            std::int64_t,
            const std::atomic_bool*)
        {
            ++persistenceCalls;
            return readyFrame(makeFrame(1, 1, 25U, 26U, 27U));
        }};
    const RepresentativeFrameStoreResult persistenceFailure = persistenceStore.loadOrGenerate(sourcePath, 0);
    const RepresentativeFrameStoreResult ramAfterPersistenceFailure =
        persistenceStore.loadOrGenerate(sourcePath, 0);

    return require(emptyRoot.status == RepresentativeFrameStoreStatus::InvalidRequest
                && emptyRootCalls == 0U,
               "an empty cache root must fail without creating a relative cache directory")
        && require(persistenceFailure.status == RepresentativeFrameStoreStatus::Generated
                && persistenceFailure.persistenceFailed && !persistenceFailure.frame.rgba.empty()
                && persistenceCalls == 1U,
            "a PNG persistence failure must still return the usable decoded frame")
        && require(ramAfterPersistenceFailure.status == RepresentativeFrameStoreStatus::RamHit
                && persistenceCalls == 1U,
            "a valid frame from a persistence failure may remain available in RAM");
}

}

int main()
{
    return verifyByteBoundedLru()
            && verifyRamDiskGenerateAndMetadata()
            && verifyCorruptPngRecoveryAndMetadataRepair()
            && verifySourceChangeCancellationAndPersistenceFailure()
            && verifyEmptyRootAndPngPersistenceFailure()
        ? 0
        : 1;
}
