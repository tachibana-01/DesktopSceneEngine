#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include "MediaResource.h"

namespace DesktopSceneEngine::Resources
{

class ImageResource final : public MediaResource
{
public:

    [[nodiscard]] MediaType type() const noexcept override;

    [[nodiscard]] bool loadFromFile(const std::filesystem::path& path);

    [[nodiscard]] const unsigned char* data() const noexcept;

    [[nodiscard]] int width() const noexcept;

    [[nodiscard]] int height() const noexcept;

    [[nodiscard]] int channels() const noexcept;

    [[nodiscard]] std::size_t rowStride() const noexcept;

    [[nodiscard]] std::size_t byteSize() const noexcept;

private:

    std::vector<unsigned char> pixels_{};
    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::size_t rowStride_ = 0;
};

}
