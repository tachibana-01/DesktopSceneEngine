#include "Core/EmbeddedAudioLifecycle.h"

#include <array>
#include <cstddef>
#include <iostream>

namespace
{

bool require(bool condition, const char* message)
{
    if (condition)
    {
        return true;
    }

    std::cerr << "Test failed: " << message << std::endl;
    return false;
}

bool verifyStopRemoveMutationOrder()
{
    enum class Operation
    {
        StopOutput,
        RemoveInput,
        MutateBorrowedResource
    };

    std::array<Operation, 3U> operations{};
    std::size_t operationCount = 0U;
    bool adapterRegistered = true;
    bool sourceMutated = false;

    DesktopSceneEngine::Core::stopOutputThenRemoveEmbeddedInput(
        [&]() noexcept
        {
            operations[operationCount++] = Operation::StopOutput;
        },
        [&]() noexcept
        {
            operations[operationCount++] = Operation::RemoveInput;
            adapterRegistered = false;
        });

    if (!require(
            operationCount == 2U
                && operations[0] == Operation::StopOutput
                && operations[1] == Operation::RemoveInput,
            "Embedded audio teardown must stop output before removing its input")
        || !require(
            !adapterRegistered,
            "Borrowed PCM mutation must be blocked until the adapter is removed"))
    {
        return false;
    }

    if (adapterRegistered)
    {
        return false;
    }

    operations[operationCount++] = Operation::MutateBorrowedResource;
    sourceMutated = true;
    return require(
        sourceMutated
            && operationCount == 3U
            && operations[2] == Operation::MutateBorrowedResource,
        "Resource mutation must occur only after stop-and-remove teardown");
}

}

int main()
{
    return verifyStopRemoveMutationOrder() ? 0 : 1;
}
