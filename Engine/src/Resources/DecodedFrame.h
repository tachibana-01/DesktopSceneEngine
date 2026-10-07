#pragma once

#include <cstddef>

namespace DesktopSceneEngine::Resources
{

struct DecodedFrame
{
    const void* data = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::size_t rowStride = 0;
    std::size_t byteSize = 0;
    double timestampSeconds = 0.0;
    bool hasTimestamp = false;
};

}
