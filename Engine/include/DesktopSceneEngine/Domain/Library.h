#pragma once

#include <vector>

#include <DesktopSceneEngine/Domain/Media.h>

namespace DesktopSceneEngine
{
namespace Domain
{

// Defines the ownership boundary for registered Media objects.
struct Library
{
    std::vector<Media> media{};
};

}
}
