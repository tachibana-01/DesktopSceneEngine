#pragma once

namespace DesktopSceneEngine
{
namespace Domain
{

// Represents declarative playback settings without executing playback.
struct Playback
{
    bool loop = true;
    float speed = 1.0F;
    float volume = 1.0F;
};

}
}
