#pragma once

#include "MediaType.h"

namespace DesktopSceneEngine::Resources
{

class MediaResource
{
public:

    virtual ~MediaResource() = default;

    [[nodiscard]] virtual MediaType type() const noexcept = 0;
};

}
