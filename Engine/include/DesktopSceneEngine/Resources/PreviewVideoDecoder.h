#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace DesktopSceneEngine::Resources
{

// An owned RGBA8 frame intended for UI-side preview consumers. rowStride is
// measured in bytes and rgba contains rowStride * height bytes.
struct PreviewFrame
{
    std::vector<std::uint8_t> rgba{};
    int width = 0;
    int height = 0;
    std::size_t rowStride = 0;
    double timestampSeconds = 0.0;
    bool hasTimestamp = false;
};

struct PreviewDecodeOptions
{
    int maximumWidth = 0;
    int maximumHeight = 0;
};

enum class PreviewDecodeStatus
{
    Ready,
    Cancelled,
    Unavailable
};

struct PreviewDecodeResult
{
    PreviewDecodeStatus status = PreviewDecodeStatus::Unavailable;
    PreviewFrame frame{};
};

struct PreviewDecodeDiagnostics
{
    std::uint64_t decodedFrames = 0U;
    std::uint64_t cancellationChecks = 0U;
    std::uint64_t cancelledRequests = 0U;
};

// Owns an isolated visual-only decoder. It does not participate in Engine
// playback state and exposes no FFmpeg implementation types.
class PreviewVideoDecoder final
{
public:

    PreviewVideoDecoder();
    ~PreviewVideoDecoder();

    PreviewVideoDecoder(const PreviewVideoDecoder&) = delete;
    PreviewVideoDecoder& operator=(const PreviewVideoDecoder&) = delete;
    PreviewVideoDecoder(PreviewVideoDecoder&&) noexcept;
    PreviewVideoDecoder& operator=(PreviewVideoDecoder&&) noexcept;

    [[nodiscard]] bool open(const std::filesystem::path& path) noexcept;

    [[nodiscard]] std::optional<PreviewFrame> frameAt(float timeSeconds) noexcept;

    [[nodiscard]] PreviewDecodeResult frameAt(
        float timeSeconds,
        const PreviewDecodeOptions& options,
        const std::atomic_bool* cancellationRequested
    ) noexcept;

    void clear() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;

    [[nodiscard]] float duration() const noexcept;

    [[nodiscard]] PreviewDecodeDiagnostics diagnostics() const noexcept;

    [[nodiscard]] std::uint64_t cancellationCheckCount() const noexcept;

private:

    struct Impl;

    std::unique_ptr<Impl> impl_{};
};

}
