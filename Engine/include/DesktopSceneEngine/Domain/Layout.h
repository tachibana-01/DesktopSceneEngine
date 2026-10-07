#pragma once

namespace DesktopSceneEngine
{
namespace Domain
{

// Represents basic scene placement data without depending on a rendering system.
struct Layout
{
    float positionX = 0.0F;
    float positionY = 0.0F;
    float scaleX = 1.0F;
    float scaleY = 1.0F;
};

}
}
