#pragma once

#include <utility>

namespace DesktopSceneEngine::Core
{

template <typename StopOutput, typename RemoveInput>
inline void stopOutputThenRemoveEmbeddedInput(
    StopOutput&& stopOutput,
    RemoveInput&& removeInput
) noexcept(
    noexcept(std::forward<StopOutput>(stopOutput)())
    && noexcept(std::forward<RemoveInput>(removeInput)())
)
{
    std::forward<StopOutput>(stopOutput)();
    std::forward<RemoveInput>(removeInput)();
}

}
