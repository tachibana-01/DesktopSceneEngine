#include "BackgroundMusicOrchestrator.h"

#include <cassert>
#include <utility>

namespace DesktopSceneEngine::Music
{

namespace
{

constexpr std::size_t MaximumActionsPerRuntimeEvent = 4U;
constexpr std::size_t ReservedUnitsPerRuntimeEvent = 1U + MaximumActionsPerRuntimeEvent;
constexpr std::size_t MaximumReservedUnitsPerDrain = 4096U;

}

BackgroundMusicOrchestrator::BackgroundMusicOrchestrator(
    BackgroundMusicRuntime& runtime,
    MusicActionExecutor executor
)
    : runtime_(runtime)
    , executor_(std::move(executor))
{
    assert(executor_);
}

MusicProcessResult BackgroundMusicOrchestrator::processMusicEvent(MusicRuntimeEvent event)
{
    if (processing_)
    {
        reentrantEvents_.push_back(std::move(event));
        return MusicProcessResult::Enqueued;
    }

    events_.push_back(std::move(event));
    processing_ = true;
    std::size_t reservedUnits = 0U;
    MusicProcessResult result = MusicProcessResult::Completed;

    while (!events_.empty())
    {
        if (reservedUnits + ReservedUnitsPerRuntimeEvent > MaximumReservedUnitsPerDrain)
        {
            result = MusicProcessResult::SafetyLimitExceeded;
            break;
        }

        reservedUnits += ReservedUnitsPerRuntimeEvent;
        MusicRuntimeEvent currentEvent = std::move(events_.front());
        events_.pop_front();
        const std::vector<MusicRuntimeAction> actions = runtime_.apply(currentEvent);
        assert(actions.size() <= MaximumActionsPerRuntimeEvent);

        std::vector<MusicRuntimeEvent> immediateFacts{};
        immediateFacts.reserve(actions.size());
        for (const MusicRuntimeAction& action : actions)
        {
            if (const std::optional<MusicRuntimeEvent> fact = executor_(action); fact.has_value())
            {
                immediateFacts.push_back(*fact);
            }
        }

        for (MusicRuntimeEvent& fact : immediateFacts)
        {
            events_.push_back(std::move(fact));
        }
        for (MusicRuntimeEvent& reentrantEvent : reentrantEvents_)
        {
            events_.push_back(std::move(reentrantEvent));
        }
        reentrantEvents_.clear();
    }

    processing_ = false;
    return result;
}

}
