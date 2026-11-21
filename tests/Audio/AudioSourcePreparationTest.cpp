#include "Audio/AudioSourcePreparation.h"
#include "Audio/AudioPcmPipe.h"
#include "Resources/SequentialAudioSource.h"
#include "Resources/AudioResource.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace DesktopSceneEngine::Audio;
namespace Resources = DesktopSceneEngine::Resources;
static_assert(AudioSourcePreparation::ReferencePrimeFrames == 9600);

namespace
{
bool require(bool value, const char* reason)
{
    if (!value) std::cerr << "FAIL: " << reason << '\n';
    return value;
}
template<class Predicate> bool waitFor(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    do
    {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
AudioSourceKey key(AudioLane lane, std::uint8_t slot, std::uint64_t incarnation)
{
    return {lane, slot, incarnation, 1, lane == AudioLane::Embedded ? 0ULL : incarnation, 9};
}
bool stop(AudioSourcePreparation& producers)
{
    producers.beginShutdown();
    return require(waitFor([&] { return producers.stopped(); }), "ordinary fixture producers truly exit")
        && require(producers.joinStopped(), "join follows exit; no timeout-free");
}

#if defined(_WIN32)
bool controlFactWake(const std::filesystem::path& flac)
{
    const std::unique_ptr<void, decltype(&CloseHandle)> wake{CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle};
    AudioSourcePreparation producers;
    const auto source = key(AudioLane::PlaylistBgm, 2, 1);
    if (!require(wake && producers.setControlWakeEvent(wake.get()), "C attaches its event before P admission")
        || !require(producers.requestBgm(source, flac) == AudioPreparationAdmission::Admitted, "real BGM source admitted")
        || !require(!producers.setControlWakeEvent(wake.get()), "P rejects event reassignment after admission")
        || !require(WaitForSingleObject(wake.get(), 2000) == WAIT_OBJECT_0, "producer wakes C at first prime"))
        return false;
    const auto primed = producers.snapshot(source);
    if (!require(primed.state == AudioPreparationState::Ready && !primed.producerQuiesced
        && producers.view(source).has_value(), "a real stream becomes Ready while its producer remains alive"))
        return false;
    producers.cancel(source);
    if (!require(WaitForSingleObject(wake.get(), 2000) == WAIT_OBJECT_0
        && producers.snapshot(source).producerQuiesced && producers.reclaim(source),
        "cancelling the live producer wakes C after record quiescence"))
        return false;

    const auto failed = key(AudioLane::ItemBgm, 4, 2);
    if (!require(producers.requestBgm(failed, flac.parent_path() / "__missing_cp3_media__.flac") == AudioPreparationAdmission::Admitted
        && WaitForSingleObject(wake.get(), 2000) == WAIT_OBJECT_0
        && producers.snapshot(failed).state == AudioPreparationState::Failed && !producers.view(failed),
        "a genuine resource failure wakes C with no prepared view"))
        return false;
    producers.beginShutdown();
    const auto deadline = GetTickCount64() + 2000;
    do
    {
        const auto now = GetTickCount64();
        if (now >= deadline || WaitForSingleObject(wake.get(), static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
            return require(false, "all producer exits wake C before the shutdown deadline");
    } while (!producers.stopped());
    return require(producers.joinStopped() && producers.reclaim(failed), "the last P fact allows real join before closing C's event");
}
#endif

bool immutableAndRetirement(const std::filesystem::path& wave)
{
    AudioSourcePreparation producers;
    const auto first = key(AudioLane::Embedded, 0, 1);
    const auto rate = key(AudioLane::Embedded, 1, 2);
    if (!require(producers.requestEmbedded(first, wave, 1) == AudioPreparationAdmission::Admitted,
        "real embedded decode admitted")
        || !require(waitFor([&] { return producers.view(first).has_value(); }), "real embedded PCM prepared")) return false;
    const auto original = *producers.view(first);
    const std::vector<std::int16_t> saved(original.pcm, original.pcm + original.sampleCount);
    if (!require(producers.markAudioSubmitted(first), "consumer exposure tracked before enqueue")
        || !require(producers.requestEmbedded(rate, wave, 1.25F, first) == AudioPreparationAdmission::Admitted,
        "tempo builds against retained immutable original")
        || !require(waitFor([&] { return producers.view(rate).has_value(); }), "real atempo candidate prepared")) return false;
    const auto replacement = *producers.view(rate);
    if (!require(std::equal(saved.begin(), saved.end(), original.pcm), "old audio lease stays immutable across real tempo build")
        || !require(replacement.tempo == 1.25 && replacement.durationSeconds == original.durationSeconds,
        "tempo output preserves media duration and exact requested tempo")) return false;
    producers.cancel(first);
    if (!require(waitFor([&] { return producers.snapshot(first).producerQuiesced; }), "producer quiescence is explicit")
        || !require(!producers.reclaim(first), "producer ACK alone cannot reclaim an exposed consumer")
        || !require(producers.markConsumerQuiesced(first) && producers.reclaim(first), "dual proof permits nondeadline reclamation")
        || !require(producers.view(rate).has_value() && replacement.pcm[0] == producers.view(rate)->pcm[0],
        "shared original survives old record reclamation")
        || !require(!producers.markConsumerQuiesced(first), "late old identity cannot mutate reclaimed record")) return false;
    const auto reused = key(AudioLane::Embedded, 0, 3);
    const auto sharedReservation = producers.reservedPcmBytes();
    if (!require(producers.requestEmbedded(reused, wave, 1.0F, rate) == AudioPreparationAdmission::Admitted,
        "fully retired record can be reused with a new incarnation")
        || !require(waitFor([&] { return producers.view(reused).has_value(); }), "reused record produces real view")
        || !require(producers.reservedPcmBytes() == sharedReservation,
            "same immutable allocation shared across records is not counted twice")
        || !require(!producers.markConsumerQuiesced(first), "ABA old identity cannot ACK the reused record")) return false;
    if (!stop(producers)) return false;
    return require(producers.reclaim(rate) && producers.reclaim(reused), "unexposed prepared candidates reclaim after producer shutdown")
        && require(producers.reservedPcmBytes() == 0, "shared allocation identity releases budget exactly once");
}

bool streaming(const std::filesystem::path& flac)
{
    AudioSourcePreparation producers;
    const auto source = key(AudioLane::PlaylistBgm, 2, 1);
    if (!require(producers.requestBgm(source, flac) == AudioPreparationAdmission::Admitted, "real streaming source admitted")
        || !require(waitFor([&] { return producers.view(source).has_value(); }), "real streaming source primed")
        || !require(producers.markAudioSubmitted(source), "stream consumer exposure recorded")) return false;
    auto* pipe = producers.view(source)->pipe;
    if (!require(pipe->availableFrames() >= AudioSourcePreparation::ReferencePrimeFrames,
        "unknown-device reference prime retains at least 200ms real PCM")) return false;
    Resources::SequentialAudioSource reference;
    if (!require(reference.open(flac), "independent real decoder reference opens")) return false;
    std::array<float, 2054> actual{}, expected{};
    std::vector<float> collected;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    bool ended = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto result = pipe->readFrames(actual);
        collected.insert(collected.end(), actual.begin(), actual.begin() + result.framesProduced * 2);
        if (result.status == AudioPcmReadStatus::EndOfStream) { ended = true; break; }
        if (!require(result.status == AudioPcmReadStatus::Data || result.status == AudioPcmReadStatus::TemporaryEmpty,
            "temporary producer shortage never turns into track failure")) return false;
        if (!result.framesProduced) std::this_thread::yield();
    }
    if (!require(ended && pipe->consumerPosition() == pipe->producedPosition(), "EOF follows all decoder/resampler PCM")) return false;
    std::size_t compared = 0;
    for (;;)
    {
        const auto result = reference.readFrames(expected);
        for (std::size_t index = 0; index < result.framesProduced * 2; ++index)
            if (!require(compared + index < collected.size()
                && std::fabs(expected[index] - collected[compared + index]) < 0.000001F,
                "async PCM matches independent real FFmpeg decode sample for sample")) return false;
        compared += result.framesProduced * 2;
        if (result.status == Resources::AudioReadStatus::EndOfStream) break;
        if (!require(result.status == Resources::AudioReadStatus::Data, "reference decoder succeeds")) return false;
    }
    if (!require(compared == collected.size(), "no decoded tail is lost or duplicated")
        || !require(waitFor([&] { return producers.snapshot(source).producerQuiesced; }), "EOF producer destroys decoder before ACK")
        || !require(!producers.reclaim(source), "EOF is not consumer retirement")
        || !require(producers.markConsumerQuiesced(source) && producers.reclaim(source), "stream dual retirement")) return false;
    return stop(producers);
}

bool fullPipeCancellationAndFailure(const std::filesystem::path& flac, const std::filesystem::path& wave)
{
    AudioSourcePreparation producers;
    const auto playlist = key(AudioLane::PlaylistBgm, 2, 1);
    const auto item = key(AudioLane::ItemBgm, 4, 2);
    if (!require(producers.requestBgm(playlist, flac) == AudioPreparationAdmission::Admitted
        && producers.requestBgm(item, flac) == AudioPreparationAdmission::Admitted, "independent BGM producer contexts admitted")
        || !require(waitFor([&] { return producers.view(playlist).has_value() && producers.view(item).has_value(); }), "both lanes prime")
        || !require(waitFor([&] { return producers.view(playlist)->pipe->availableFrames() == AudioPcmPipe::CapacityFrames; }),
            "unconsumed pipe reaches fixed capacity")
        || !require(!producers.snapshot(playlist).producerQuiesced, "full live producer has not falsely ACKed")) return false;
    producers.cancel(playlist);
    if (!require(waitFor([&] { return producers.snapshot(playlist).producerQuiesced; }), "cancel wakes full-pipe resource wait")
        || !require(producers.snapshot(playlist).state == AudioPreparationState::Cancelled, "cancel is not decode failure")
        || !require(producers.reclaim(playlist), "never-published consumer proof plus producer ACK allows reclaim")) return false;
    producers.cancel(item);
    if (!require(waitFor([&] { return producers.snapshot(item).producerQuiesced; }) && producers.reclaim(item), "second lane retires independently")) return false;
    const auto shortTrack = key(AudioLane::ItemBgm, 4, 3);
    if (!require(producers.requestBgm(shortTrack, wave) == AudioPreparationAdmission::Admitted,
        "short real track admitted below reference threshold")
        || !require(waitFor([&] { return producers.snapshot(shortTrack).producerQuiesced; }), "short track drains decoder")
        || !require(producers.view(shortTrack).has_value()
            && producers.view(shortTrack)->pipe->producerTerminal()
            && producers.view(shortTrack)->pipe->availableFrames() > 0
            && producers.view(shortTrack)->pipe->availableFrames() < AudioSourcePreparation::ReferencePrimeFrames,
            "confirmed short EOF with valid PCM primes without waiting for 9600 frames")
        || !require(producers.reclaim(shortTrack), "unexposed short track reclaims after real producer ACK")) return false;
    const auto missing = key(AudioLane::Embedded, 0, 3);
    if (!require(producers.requestEmbedded(missing, wave.parent_path() / "__missing_cp1_media__.wav", 1) == AudioPreparationAdmission::Admitted,
        "failed prepare is an admitted resource outcome")
        || !require(waitFor([&] { return producers.snapshot(missing).producerQuiesced; }), "failed resource owner exits")
        || !require(producers.snapshot(missing).state == AudioPreparationState::Failed && !producers.view(missing), "failure never exposes a fake prepared view")
        || !require(producers.reclaim(missing), "failed unexposed preparation still reclaims normally")) return false;
    std::atomic_bool cancelled{true};
    Resources::AudioResource embedded;
    Resources::SequentialAudioSource bgm;
    if (!require(!embedded.open(wave, &cancelled) && !bgm.open(flac, &cancelled), "pre-cancelled real backends do not open/decode")) return false;
    return stop(producers);
}
}

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3) { std::cerr << "Expected test audio fixture directory\n"; return 2; }
    const std::filesystem::path fixtures{argv[1]};
    const auto wave = fixtures / "SequentialAudioSourceStereo48k.wav";
    const auto flac = fixtures / "SequentialAudioSourceCompressed.flac";
#if defined(_WIN32)
    if (argc == 3)
    {
        if (std::string_view{argv[2]} != "control-fact-wake" || !controlFactWake(flac)) return 1;
        std::cout << "AudioSourcePreparation control fact wake PASS\n";
        return 0;
    }
    if (!controlFactWake(flac)) return 1;
#else
    if (argc != 2) return 2;
#endif
    if (!immutableAndRetirement(flac) || !streaming(flac) || !fullPipeCancellationAndFailure(flac, wave)) return 1;
    std::cout << "AudioSourcePreparationTest PASS\n";
}
