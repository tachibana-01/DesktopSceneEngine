#pragma once

namespace DesktopSceneEngine
{
namespace Domain
{

// Represents a normalized crop region without performing image processing.
struct Crop
{
    float x = 0.0F;
    float y = 0.0F;
    float width = 1.0F;
    float height = 1.0F;
};

}
}
