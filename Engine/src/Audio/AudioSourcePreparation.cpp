#include "AudioSourcePreparation.h"
#include "AudioPcmPipe.h"
#include "Resources/AudioResource.h"
#include "Resources/SequentialAudioSource.h"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace DesktopSceneEngine::Audio
{
namespace
{
using Stage = BackgroundMusicObservation::FailureStage;
using Code = BackgroundMusicObservation::FailureCode;
constexpr std::size_t PreparationScratchBytes = 16ULL * 1024 * 1024;
constexpr std::size_t OpenReservationBytes = 3 * AudioSourcePreparation::MaximumPcmArtifactBytes + PreparationScratchBytes;
// Vector growth can briefly retain its old and replacement allocation. Reserve
// both before preparation; this is not just the final output's size budget.
constexpr std::size_t TempoReservationBytes = 2 * AudioSourcePreparation::MaximumPcmArtifactBytes + PreparationScratchBytes;

struct PcmBudget
{
    std::atomic_size_t reserved{0};
    bool acquire(std::size_t bytes) noexcept
    {
        auto current = reserved.load(std::memory_order_relaxed);
        while (bytes <= AudioSourcePreparation::MaximumEmbeddedPcmBytes - current)
            if (reserved.compare_exchange_weak(current, current + bytes,
                std::memory_order_acq_rel, std::memory_order_relaxed)) return true;
        return false;
    }
};

struct PcmReservation
{
    std::shared_ptr<PcmBudget> budget;
    std::size_t bytes = 0;
    ~PcmReservation() { if (bytes) budget->reserved.fetch_sub(bytes, std::memory_order_acq_rel); }
};

struct PcmAllocation
{
    std::vector<std::int16_t> samples;
    std::shared_ptr<PcmBudget> budget;
    std::size_t bytes = 0;
    ~PcmAllocation()
    {
        // All final allocation owners are P/C/R, never the borrowed A view.
        std::vector<std::int16_t>{}.swap(samples);
        if (bytes) budget->reserved.fetch_sub(bytes, std::memory_order_acq_rel);
    }
};

std::shared_ptr<const PcmAllocation> seal(std::vector<std::int16_t>&& samples, PcmReservation& reservation)
{
    const auto bytes = samples.capacity() * sizeof(std::int16_t);
    if (bytes > AudioSourcePreparation::MaximumPcmArtifactBytes || bytes > reservation.bytes) return {};
    auto result = std::make_shared<PcmAllocation>();
    result->samples = std::move(samples);
    result->budget = reservation.budget;
    result->bytes = bytes;
    reservation.bytes -= bytes; // Transfer the same reservation; no double count.
    return result;
}

Code sourceFailureCode(Resources::AudioSourceStatus status) noexcept
{
    switch (status)
    {
    case Resources::AudioSourceStatus::OpenFailure: return Code::OpenFailure;
    case Resources::AudioSourceStatus::NoAudioStream: return Code::NoAudioStream;
    case Resources::AudioSourceStatus::DecoderUnavailable: return Code::DecoderUnavailable;
    case Resources::AudioSourceStatus::DecodeFailure: return Code::DecodeFailure;
    case Resources::AudioSourceStatus::ResampleFailure: return Code::ResampleFailure;
    case Resources::AudioSourceStatus::ResourceFailure: return Code::ResourceFailure;
    default: return Code::Unknown;
    }
}
}

struct AudioSourcePreparation::Impl
{
    struct Record
    {
        AudioSourceKey key{};
        std::uint64_t lastIncarnation = 0; // C only, retained across reclamation.
        std::atomic<AudioPreparationState> state{AudioPreparationState::Empty};
        std::atomic_bool cancel{false};
        std::atomic_bool producerQuiesced{true};
        bool consumerQuiesced = true; // C only; A acknowledges through its own receipt.
        std::atomic<Stage> failureStage{Stage::Unknown};
        std::atomic<Code> failureCode{Code::Unknown};
        std::filesystem::path path;
        float tempo = 1.0F;
        std::size_t primeFrames = ReferencePrimeFrames;
        std::shared_ptr<const PcmAllocation> original;
        std::shared_ptr<const PcmAllocation> output;
        std::unique_ptr<PcmReservation> reservation;
        std::unique_ptr<AudioPcmPipe> pipe;
    };

    struct Context
    {
        std::mutex mutex;
        std::condition_variable changed;
        Record* pending = nullptr;
        Record* running = nullptr;
        bool stop = false;
        std::atomic_bool exited{false};
        std::thread thread;
    };

    std::shared_ptr<PcmBudget> budget = std::make_shared<PcmBudget>();
    std::array<Record, AudioSourceRecordCount> records;
    std::array<Context, AudioLaneCount> contexts;
    bool shutdown = false; // C only.
    bool admittedWork = false; // C only.
    std::atomic<void*> controlWakeEvent{nullptr};

    Impl()
    {
        try
        {
            for (auto& context : contexts)
                context.thread = std::thread([this, pointer = &context] { run(*pointer); });
        }
        catch (...)
        {
            stop();
            for (auto& context : contexts) if (context.thread.joinable()) context.thread.join();
            throw;
        }
    }

    ~Impl()
    {
        // Correct product Close polls stopped()/joins before destruction. This
        // fallback retains safety on exceptional unwinding: never detach/free
        // a live producer, and never pretend a cancellation timeout is an ACK.
        stop();
        for (auto& context : contexts) if (context.thread.joinable()) context.thread.join();
        for (const auto& record : records) assert(record.consumerQuiesced);
    }

    Record* find(const AudioSourceKey& key) noexcept
    {
        if (!key.valid()) return nullptr;
        auto& record = records[key.slot];
        return record.state.load(std::memory_order_acquire) != AudioPreparationState::Empty && record.key == key
            ? &record : nullptr;
    }
    const Record* find(const AudioSourceKey& key) const noexcept
    {
        return const_cast<Impl*>(this)->find(key);
    }

    void stop() noexcept
    {
        shutdown = true;
        for (auto& record : records) record.cancel.store(true, std::memory_order_release);
        for (auto& context : contexts)
        {
            { std::lock_guard lock{context.mutex}; context.stop = true; }
            context.changed.notify_all();
        }
    }

    void wakeControl() const noexcept
    {
#if defined(_WIN32)
        if (const auto event = controlWakeEvent.load(std::memory_order_acquire))
            SetEvent(static_cast<HANDLE>(event));
#endif
    }

    void publishPreparation(Record& record, AudioPreparationState state) noexcept
    {
        record.state.store(state, std::memory_order_release);
        wakeControl();
    }

    void fail(Record& record, Stage stage, Code code) noexcept
    {
        record.failureStage.store(stage, std::memory_order_relaxed);
        record.failureCode.store(code, std::memory_order_relaxed);
        publishPreparation(record, AudioPreparationState::Failed);
    }

    void embedded(Record& record)
    {
        if (!record.original)
        {
            Resources::AudioResource resource;
            if (!resource.open(record.path, &record.cancel))
            {
                fail(record, Stage::SourceOpen, resource.openStatus() == Resources::AudioOpenStatus::NoAudioStream
                    ? Code::NoAudioStream : Code::DecodeFailure);
                return;
            }
            auto storage = resource.releasePcm();
            record.original = seal(std::move(storage.original), *record.reservation);
            if (!record.original) { fail(record, Stage::ResourceAllocation, Code::ResourceFailure); return; }
            // Discard the resource's duplicate unit-tempo output before a new
            // tempo build. Otherwise old+candidate vector growth could exceed
            // even a truthful aggregate reservation during the build itself.
            std::vector<std::int16_t>{}.swap(storage.output);
        }
        if (std::fabs(record.tempo - 1.0F) < 0.0001F) record.output = record.original;
        else
        {
            std::vector<std::int16_t> output;
            if (!Resources::AudioResource::prepareTempoPcm(record.original->samples, record.tempo, output, &record.cancel))
            { fail(record, Stage::ResourceAllocation, Code::ResourceFailure); return; }
            record.output = seal(std::move(output), *record.reservation);
        }
        if (!record.output || record.output->samples.empty())
        { fail(record, Stage::ResourceAllocation, Code::ResourceFailure); return; }
        record.reservation.reset();
        if (!record.cancel.load(std::memory_order_acquire))
            publishPreparation(record, AudioPreparationState::Ready);
    }

    void maybeReady(Record& record, bool terminal = false) noexcept
    {
        if (!record.cancel.load(std::memory_order_acquire)
            && record.state.load(std::memory_order_relaxed) != AudioPreparationState::Ready
            && (record.pipe->availableFrames() >= record.primeFrames
                || (terminal && record.pipe->producedPosition() > 0)))
            publishPreparation(record, AudioPreparationState::Ready);
    }

    void streaming(Record& record, Context& context)
    {
        Resources::SequentialAudioSource source;
        if (!source.open(record.path, &record.cancel))
        {
            if (!record.cancel.load(std::memory_order_acquire))
            {
                const auto code = sourceFailureCode(source.status());
                (void)record.pipe->publishTerminal(AudioPcmReadStatus::Failure, Stage::SourceOpen, code);
                fail(record, Stage::SourceOpen, code);
            }
            return;
        }
        std::array<float, 2048 * AudioPcmPipe::Channels> scratch;
        while (!record.cancel.load(std::memory_order_acquire))
        {
            const auto result = source.readFrames(scratch);
            std::size_t written = 0;
            while (written < result.framesProduced && !record.cancel.load(std::memory_order_acquire))
            {
                written += record.pipe->writeFrames(std::span<const float>{scratch}.subspan(
                    written * AudioPcmPipe::Channels,
                    (result.framesProduced - written) * AudioPcmPipe::Channels));
                maybeReady(record);
                if (written < result.framesProduced)
                {
                    // Only P sleeps. A publishes its read cursor without any
                    // event/callback/lock; cancel wakes this bounded space wait.
                    std::unique_lock lock{context.mutex};
                    context.changed.wait_for(lock, std::chrono::milliseconds{2}, [&] {
                        return record.cancel.load(std::memory_order_acquire) || context.stop;
                    });
                }
            }
            if (record.cancel.load(std::memory_order_acquire)) return;
            if (result.status == Resources::AudioReadStatus::EndOfStream)
            {
                if (record.pipe->producedPosition() == 0)
                {
                    (void)record.pipe->publishTerminal(AudioPcmReadStatus::Failure, Stage::Playback, Code::DecodeFailure);
                    fail(record, Stage::Playback, Code::DecodeFailure);
                }
                else
                {
                    (void)record.pipe->publishTerminal(AudioPcmReadStatus::EndOfStream);
                    maybeReady(record, true);
                }
                return;
            }
            if (result.status != Resources::AudioReadStatus::Data || result.framesProduced == 0)
            {
                const auto code = sourceFailureCode(source.status());
                (void)record.pipe->publishTerminal(AudioPcmReadStatus::Failure, Stage::Playback, code);
                maybeReady(record, true);
                // Once Ready, A alone reports the failure at the final PCM
                // position. Do not emit a second prepare failure for it.
                if (record.state.load(std::memory_order_relaxed) != AudioPreparationState::Ready)
                    fail(record, Stage::Playback, code);
                return;
            }
        }
    }

    void run(Context& context) noexcept
    {
        for (;;)
        {
            Record* record;
            {
                std::unique_lock lock{context.mutex};
                context.changed.wait(lock, [&] { return context.pending || context.stop; });
                if (!context.pending && context.stop) break;
                record = std::exchange(context.pending, nullptr);
                context.running = record;
            }
            record->state.store(AudioPreparationState::Preparing, std::memory_order_release);
            try
            {
                if (!record->cancel.load(std::memory_order_acquire))
                {
                    if (record->key.lane == AudioLane::Embedded) embedded(*record);
                    else streaming(*record, context);
                }
            }
            catch (...)
            {
                if (record->pipe)
                    (void)record->pipe->publishTerminal(AudioPcmReadStatus::Failure, Stage::ResourceAllocation, Code::AllocationFailure);
                if (record->state.load(std::memory_order_relaxed) != AudioPreparationState::Ready)
                    fail(*record, Stage::ResourceAllocation, Code::AllocationFailure);
            }
            if (record->cancel.load(std::memory_order_acquire))
            {
                if (record->pipe) (void)record->pipe->publishTerminal(AudioPcmReadStatus::Retired);
                record->state.store(AudioPreparationState::Cancelled, std::memory_order_release);
            }
            record->reservation.reset();
            {
                std::lock_guard lock{context.mutex};
                context.running = nullptr;
            }
            // Absolutely the last record access by P. All decoder/scratch
            // destructors and captured endpoint references have already exited.
            record->producerQuiesced.store(true, std::memory_order_release);
            wakeControl();
        }
        context.exited.store(true, std::memory_order_release);
        wakeControl();
    }

    AudioPreparationAdmission request(const AudioSourceKey& key, const std::filesystem::path& path,
        float tempo, const AudioSourceKey& originalSource, std::size_t primeFrames)
    {
        if (shutdown) return AudioPreparationAdmission::Stopped;
        if (!key.valid() || path.empty() || !std::isfinite(tempo) || tempo < 0.5F || tempo > 2.0F
            || (key.lane == AudioLane::Embedded ? !key.mediaBinding : !key.sourceToken)
            || !primeFrames || primeFrames > AudioPcmPipe::CapacityFrames) return AudioPreparationAdmission::InvalidRequest;
        auto& record = records[key.slot];
        if (record.state.load(std::memory_order_acquire) != AudioPreparationState::Empty)
            return AudioPreparationAdmission::Busy;
        if (key.incarnation <= record.lastIncarnation) return AudioPreparationAdmission::InvalidRequest;
        auto& context = contexts[static_cast<std::size_t>(key.lane)];
        std::unique_lock lock{context.mutex};
        if (context.pending) return AudioPreparationAdmission::Busy;
        try
        {
            std::shared_ptr<const PcmAllocation> original;
            if (originalSource.valid())
            {
                const auto* previous = find(originalSource);
                if (key.lane != AudioLane::Embedded || originalSource.mediaBinding != key.mediaBinding
                    || !previous || previous->state.load(std::memory_order_acquire) != AudioPreparationState::Ready
                    || !previous->original) return AudioPreparationAdmission::InvalidRequest;
                original = previous->original;
            }
            std::unique_ptr<PcmReservation> reservation;
            std::unique_ptr<AudioPcmPipe> pipe;
            if (key.lane == AudioLane::Embedded)
            {
                reservation = std::make_unique<PcmReservation>();
                reservation->budget = budget;
                const auto bytes = original
                    ? (std::fabs(tempo - 1.0F) < 0.0001F ? std::size_t{0} : TempoReservationBytes)
                    : OpenReservationBytes;
                if (!budget->acquire(bytes)) return AudioPreparationAdmission::ResourceLimit;
                reservation->bytes = bytes;
            }
            else pipe = std::make_unique<AudioPcmPipe>();
            record.path = path;
            record.key = key;
            record.lastIncarnation = key.incarnation;
            record.tempo = tempo;
            record.primeFrames = primeFrames;
            record.original = std::move(original);
            record.reservation = std::move(reservation);
            record.pipe = std::move(pipe);
            record.cancel.store(false, std::memory_order_relaxed);
            record.producerQuiesced.store(false, std::memory_order_relaxed);
            record.consumerQuiesced = true; // No A-visible reference yet.
            record.failureStage.store(Stage::Unknown, std::memory_order_relaxed);
            record.failureCode.store(Code::Unknown, std::memory_order_relaxed);
            record.state.store(AudioPreparationState::Queued, std::memory_order_release);
            admittedWork = true;
            context.pending = &record;
            lock.unlock();
            context.changed.notify_one();
            return AudioPreparationAdmission::Admitted;
        }
        catch (...)
        {
            return AudioPreparationAdmission::ResourceLimit;
        }
    }
};

AudioSourcePreparation::AudioSourcePreparation() : impl_(std::make_unique<Impl>()) {}
AudioSourcePreparation::~AudioSourcePreparation() = default;

bool AudioSourcePreparation::setControlWakeEvent(void* borrowedEvent) noexcept
{
#if defined(_WIN32)
    if (!borrowedEvent || impl_->admittedWork || impl_->shutdown) return false;
    void* unset = nullptr;
    return impl_->controlWakeEvent.compare_exchange_strong(unset, borrowedEvent,
        std::memory_order_release, std::memory_order_relaxed);
#else
    (void)borrowedEvent;
    return false;
#endif
}

AudioPreparationAdmission AudioSourcePreparation::requestEmbedded(const AudioSourceKey& key,
    const std::filesystem::path& path, float tempo, const AudioSourceKey& originalSource)
{
    if (key.lane != AudioLane::Embedded) return AudioPreparationAdmission::InvalidRequest;
    return impl_->request(key, path, tempo, originalSource, ReferencePrimeFrames);
}
AudioPreparationAdmission AudioSourcePreparation::requestBgm(const AudioSourceKey& key,
    const std::filesystem::path& path, std::size_t primeFrames)
{
    if (key.lane == AudioLane::Embedded) return AudioPreparationAdmission::InvalidRequest;
    return impl_->request(key, path, 1.0F, {}, primeFrames);
}

AudioPreparationSnapshot AudioSourcePreparation::snapshot(const AudioSourceKey& key) const noexcept
{
    const auto* record = impl_->find(key);
    if (!record) return {};
    return {key, record->state.load(std::memory_order_acquire), record->cancel.load(std::memory_order_acquire),
        record->producerQuiesced.load(std::memory_order_acquire), record->consumerQuiesced,
        record->failureStage.load(std::memory_order_acquire), record->failureCode.load(std::memory_order_acquire)};
}

std::optional<PreparedAudioSourceView> AudioSourcePreparation::view(const AudioSourceKey& key) const noexcept
{
    const auto* record = impl_->find(key);
    if (!record || record->state.load(std::memory_order_acquire) != AudioPreparationState::Ready
        || record->cancel.load(std::memory_order_acquire)) return std::nullopt;
    PreparedAudioSourceView view;
    view.key = key;
    if (record->pipe) view.pipe = record->pipe.get();
    else if (record->output && record->original)
    {
        view.pcm = record->output->samples.data();
        view.sampleCount = record->output->samples.size();
        view.tempo = record->tempo;
        view.durationSeconds = static_cast<double>(record->original->samples.size() / 2) / 48000.0;
    }
    return view.valid() ? std::optional{view} : std::nullopt;
}

bool AudioSourcePreparation::markAudioSubmitted(const AudioSourceKey& key) noexcept
{
    auto* record = impl_->find(key);
    if (!record || !view(key) || !record->consumerQuiesced) return false;
    record->consumerQuiesced = false;
    return true;
}
bool AudioSourcePreparation::markConsumerQuiesced(const AudioSourceKey& key) noexcept
{
    auto* record = impl_->find(key);
    if (!record) return false;
    record->consumerQuiesced = true;
    return true;
}
void AudioSourcePreparation::cancel(const AudioSourceKey& key) noexcept
{
    if (auto* record = impl_->find(key))
    {
        record->cancel.store(true, std::memory_order_release);
        impl_->contexts[static_cast<std::size_t>(key.lane)].changed.notify_all();
    }
}
bool AudioSourcePreparation::reclaim(const AudioSourceKey& key) noexcept
{
    auto* record = impl_->find(key);
    if (!record || !record->consumerQuiesced || !record->producerQuiesced.load(std::memory_order_acquire)) return false;
    record->output.reset();
    record->original.reset();
    record->pipe.reset();
    record->reservation.reset();
    record->path.clear();
    record->key = {};
    record->state.store(AudioPreparationState::Empty, std::memory_order_release);
    return true;
}
void AudioSourcePreparation::beginShutdown() noexcept { impl_->stop(); }
bool AudioSourcePreparation::stopped() const noexcept
{
    for (const auto& context : impl_->contexts)
        if (!context.exited.load(std::memory_order_acquire)) return false;
    return true;
}
bool AudioSourcePreparation::joinStopped() noexcept
{
    if (!stopped()) return false;
    for (auto& context : impl_->contexts) if (context.thread.joinable()) context.thread.join();
    return true;
}
std::size_t AudioSourcePreparation::reservedPcmBytes() const noexcept
{
    return impl_->budget->reserved.load(std::memory_order_acquire);
}
}
