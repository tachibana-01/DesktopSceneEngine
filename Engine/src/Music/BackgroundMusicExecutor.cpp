#include "BackgroundMusicExecutor.h"

#include "Audio/AudioMixerInput.h"
#include "Music/SequentialAudioMixerInput.h"
#include "Resources/SequentialAudioSource.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>

namespace DesktopSceneEngine::Music
{

namespace
{

[[nodiscard]] int scopeSortValue(MusicScope scope) noexcept
{
    switch (scope)
    {
    case MusicScope::PlaylistGlobal:
        return 0;

    case MusicScope::Item:
        return 1;
    }

    return 2;
}

[[nodiscard]] bool isValidScope(MusicScope scope) noexcept
{
    return scope == MusicScope::PlaylistGlobal || scope == MusicScope::Item;
}

[[nodiscard]] bool isValidActivity(PlaybackActivity activity) noexcept
{
    return activity == PlaybackActivity::Playing || activity == PlaybackActivity::Paused;
}

[[nodiscard]] MusicSourceFailureClass classifySourceOpenFailure(
    Resources::AudioSourceStatus status
) noexcept
{
    return status == Resources::AudioSourceStatus::ResourceFailure
        ? MusicSourceFailureClass::TransientResourceFailure
        : MusicSourceFailureClass::UnplayableTrack;
}

}

BackgroundMusicExecutor::BackgroundMusicExecutor(Audio::AudioMixer& mixer) noexcept
    : BackgroundMusicExecutor(mixer, &BackgroundMusicExecutor::registerWithMixer)
{
}

BackgroundMusicExecutor::BackgroundMusicExecutor(
    Audio::AudioMixer& mixer,
    InputRegistrar registrar
) noexcept
    : mixer_(mixer)
    , registrar_(registrar)
{
    assert(registrar_ != nullptr);
}

BackgroundMusicExecutor::~BackgroundMusicExecutor()
{
    for (const ActiveMusicSource& source : activeSources_)
    {
        (void)mixer_.removeInput(source.mixerHandle);
    }
    activeSources_.clear();
}

std::optional<MusicRuntimeEvent> BackgroundMusicExecutor::execute(
    const MusicRuntimeAction& action
) noexcept
{
    return std::visit(
        [this](const auto& typedAction)
        {
            return executeAction(typedAction);
        },
        action);
}

std::vector<MusicRuntimeEvent> BackgroundMusicExecutor::collectTerminalEvents()
{
    std::vector<MusicRuntimeEvent> events{};
    try
    {
        events.reserve(activeSources_.size());
    }
    catch (const std::bad_alloc&)
    {
        return {};
    }
    catch (const std::length_error&)
    {
        return {};
    }

    std::sort(
        activeSources_.begin(),
        activeSources_.end(),
        [](const ActiveMusicSource& left, const ActiveMusicSource& right)
        {
            const int leftScope = scopeSortValue(left.scope);
            const int rightScope = scopeSortValue(right.scope);
            return leftScope != rightScope
                ? leftScope < rightScope
                : left.token.value < right.token.value;
        });

    std::size_t index = 0U;
    while (index < activeSources_.size())
    {
        const ActiveMusicSource source = activeSources_[index];
        Audio::AudioInputState terminalState = Audio::AudioInputState::Active;
        if (!mixer_.consumeTerminalState(source.mixerHandle, terminalState))
        {
            ++index;
            continue;
        }

        removeActiveSource(activeSources_.begin() + static_cast<std::ptrdiff_t>(index));

        switch (terminalState)
        {
        case Audio::AudioInputState::Ended:
            events.emplace_back(MusicSourceEnded{source.token});
            break;

        case Audio::AudioInputState::Failed:
            events.emplace_back(MusicSourceFailed{
                source.token,
                MusicSourceFailureClass::UnplayableTrack});
            break;

        case Audio::AudioInputState::Active:
            assert(false);
            break;
        }
    }

    return events;
}

bool BackgroundMusicExecutor::hasActiveSources() const noexcept
{
    return !activeSources_.empty();
}

std::optional<MusicRuntimeEvent> BackgroundMusicExecutor::executeAction(
    const StartSource& action
) noexcept
{
    if (!isValidStartAction(action) || findActiveSource(action.token) != activeSources_.end())
    {
        assert(false);
        return std::nullopt;
    }

    std::unique_ptr<Resources::SequentialAudioSource> source{};
    try
    {
        source = std::make_unique<Resources::SequentialAudioSource>();
    }
    catch (const std::bad_alloc&)
    {
        return MusicSourceFailed{action.token, MusicSourceFailureClass::TransientResourceFailure};
    }

    if (!source->open(action.sourcePath))
    {
        return MusicSourceFailed{
            action.token,
            classifySourceOpenFailure(source->status())};
    }

    std::unique_ptr<SequentialAudioMixerInput> input{};
    try
    {
        input = std::make_unique<SequentialAudioMixerInput>(std::move(source));
    }
    catch (const std::bad_alloc&)
    {
        return MusicSourceFailed{action.token, MusicSourceFailureClass::TransientResourceFailure};
    }

    try
    {
        activeSources_.push_back({action.token, {}, action.scope});
    }
    catch (const std::bad_alloc&)
    {
        return MusicSourceFailed{action.token, MusicSourceFailureClass::TransientResourceFailure};
    }
    catch (const std::length_error&)
    {
        return MusicSourceFailed{action.token, MusicSourceFailureClass::TransientResourceFailure};
    }

    const Audio::AudioInputHandle handle = registrar_(
        mixer_,
        std::move(input),
        action.effectiveGain);
    if (!handle)
    {
        activeSources_.pop_back();
        return MusicSourceFailed{action.token, MusicSourceFailureClass::TransientResourceFailure};
    }

    activeSources_.back().mixerHandle = handle;
    return MusicSourceStarted{action.token};
}

std::optional<MusicRuntimeEvent> BackgroundMusicExecutor::executeAction(
    const StopSource& action
) noexcept
{
    const auto source = findActiveSource(action.token);
    if (source != activeSources_.end())
    {
        removeActiveSource(source);
    }

    return std::nullopt;
}

std::optional<MusicRuntimeEvent> BackgroundMusicExecutor::executeAction(
    const SetSourceGain& action
) noexcept
{
    if (!action.token || !isValidGain(action.gain))
    {
        assert(false);
        return std::nullopt;
    }

    const auto source = findActiveSource(action.token);
    if (source == activeSources_.end())
    {
        return std::nullopt;
    }

    if (!mixer_.setInputGain(source->mixerHandle, action.gain))
    {
        assert(false);
        activeSources_.erase(source);
    }

    return std::nullopt;
}

Audio::AudioInputHandle BackgroundMusicExecutor::registerWithMixer(
    Audio::AudioMixer& mixer,
    std::unique_ptr<Audio::AudioMixerInput> input,
    float gain
) noexcept
{
    return mixer.addInput(std::move(input), gain);
}

bool BackgroundMusicExecutor::isValidStartAction(const StartSource& action) noexcept
{
    return static_cast<bool>(action.token)
        && !action.sourcePath.empty()
        && isValidGain(action.effectiveGain)
        && isValidScope(action.scope)
        && isValidActivity(action.activity);
}

bool BackgroundMusicExecutor::isValidGain(float gain) noexcept
{
    return std::isfinite(gain) && gain >= 0.0F && gain <= 1.0F;
}

std::vector<BackgroundMusicExecutor::ActiveMusicSource>::iterator
BackgroundMusicExecutor::findActiveSource(MusicSourceToken token) noexcept
{
    return std::find_if(
        activeSources_.begin(),
        activeSources_.end(),
        [token](const ActiveMusicSource& source)
        {
            return source.token == token;
        });
}

void BackgroundMusicExecutor::removeActiveSource(
    std::vector<ActiveMusicSource>::iterator source
) noexcept
{
    const bool removed = mixer_.removeInput(source->mixerHandle);
    assert(removed);
    activeSources_.erase(source);
}

}
