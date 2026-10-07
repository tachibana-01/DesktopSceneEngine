#pragma once

#include <string>

#include <DesktopSceneEngine/Domain/EntityId.h>

namespace DesktopSceneEngine
{
namespace Domain
{

// Represents a registered media resource without loading or decoding it.
struct Media
{
    EntityId id{};
    std::string name{};
    std::string resourceReference{};
};

}
}
