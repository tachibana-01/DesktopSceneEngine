// CP1 coverage migration of the six integration groups below. The same A loop,
// mixer, prepared sources, durable receipts and sink conversion are exercised.
// No C-thread mixer pull or production output hook remains.
#include "AudioExecutionEngineFixture.h"
#include <DesktopSceneEngine/BackgroundMusicControl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace
{
using namespace DesktopSceneEngine;
using namespace BackgroundMusicControl;
namespace T = Tests;
using Access = Core::AudioOutputRecoveryEngineTestAccess;
using Lane = Audio::AudioLane;
using Phase = Music::MusicSessionPhase;
using Health = Audio::AudioOutputHealth;

bool require(bool value, const char* message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
std::filesystem::path fixturePath()
{ return std::filesystem::path{DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR} / "SequentialAudioSourceStereo48k.wav"; }
TrackSnapshot track(std::string id, std::filesystem::path path = fixturePath(), double gain = 1)
{ return {TrackId{std::move(id)}, std::move(path), gain}; }
QueueSnapshot queue(std::vector<TrackSnapshot> tracks, double gain = 1, QueueMode mode = QueueMode::ListLoop)
{ return {std::move(tracks), mode, gain}; }
ActiveScopeSnapshot scope(std::string playlist, QueueSnapshot playlistQueue,
    std::optional<ItemId> item = {}, std::optional<QueueSnapshot> itemQueue = {})
{ return {PlaylistId{std::move(playlist)}, std::move(playlistQueue), std::move(item), std::move(itemQueue)}; }
const Music::MusicSession* playlistSession(const Engine& engine)
{ return Core::BackgroundMusicEngineTestAccess::runtime(engine).playlistSession(); }
const Music::MusicSession* itemSession(const Engine& engine)
{ return Core::BackgroundMusicEngineTestAccess::runtime(engine).itemSession(); }
auto playlistToken(const Engine& engine)
{ return playlistSession(engine) ? playlistSession(engine)->sourceToken() : std::optional<Music::MusicSourceToken>{}; }
auto itemToken(const Engine& engine)
{ return itemSession(engine) ? itemSession(engine)->sourceToken() : std::optional<Music::MusicSourceToken>{}; }
bool liveBgm(Engine& engine)
{ return T::lane(engine, Lane::PlaylistBgm) || T::lane(engine, Lane::ItemBgm); }
bool pausePlaybackAndMusic(T::AwaitAudioEngine& engine)
{
    engine.togglePause();
    return engine.isPaused() && engine.processBackgroundMusicControl(PlaybackPaused{});
}
bool resumePlaybackAndMusic(T::AwaitAudioEngine& engine)
{
    engine.togglePause();
    return !engine.isPaused() && engine.processBackgroundMusicControl(PlaybackResumed{});
}
bool prepare(T::AwaitAudioEngine& engine, const Domain::Project& project, bool paused = false)
{ Access::preparePolicy(engine, project, 600, paused); return T::settle(engine); }
bool embedded(T::AwaitAudioEngine& engine)
{
    return T::effect(engine, [&] { Access::prepareEmbedded(engine, fixturePath()); return true; })
        && T::lane(engine, Lane::Embedded).has_value();
}
bool pull(T::AwaitAudioEngine& engine, std::size_t frames = 1)
{
    std::vector<float> samples(frames * 2);
    return T::readOutput(engine, engine.device, samples) && T::settle(engine);
}
float quantized(float value)
{
    const auto sample = value >= 1 ? 32767 : value <= -1 ? -32768
        : static_cast<int>(std::lround(value * (value < 0 ? 32768.0F : 32767.0F)));
    return sample < 0 ? static_cast<float>(sample) / 32768 : static_cast<float>(sample) / 32767;
}
bool stereo(T::AwaitAudioEngine& engine, float left, float right)
{
    const auto before = T::audioState(engine);
    std::array<float, 2> actual{};
    if (!T::readOutput(engine, engine.device, actual)) return false;
    const bool matches = std::fabs(actual[0] - quantized(left)) < 0.00001F
        && std::fabs(actual[1] - quantized(right)) < 0.00001F;
    if (!matches)
    {
        const auto& control = Access::control(engine);
        std::cerr << "Mix actual=[" << actual[0] << ',' << actual[1]
            << "] expected=[" << quantized(left) << ',' << quantized(right)
            << "] clock.position=" << control.clock.position << " mode=" << static_cast<unsigned>(control.clock.mode)
            << " transport=" << static_cast<unsigned>(before.transport)
            << " cursors=[" << before.laneCursorFrames[0] << ',' << before.laneCursorFrames[1]
            << ',' << before.laneCursorFrames[2] << "]\n";
    }
    return require(matches, "same A mixer/bridge output has exactly expected independent contributions and S16 quantization");
}

bool verifyItemLifecycleAndSameItemLoop()
{
    Domain::Project project{}; T::AwaitAudioEngine engine;
    if (!prepare(engine, project)) return false;
    const auto itemQueue = queue({track("item-a-track")});
    const auto started = PlaybackSessionStarted{scope("playlist", queue({}), ItemId{"item-a"}, itemQueue)};
    if (!require(engine.processBackgroundMusicControl(started) && T::lane(engine, Lane::ItemBgm)
        && pull(engine, 16), "Item-only start registers and consumes actual prepared PCM")) return false;
    const auto original = itemToken(engine); const auto source = T::lane(engine, Lane::ItemBgm)->source;
    if (!require(original && pausePlaybackAndMusic(engine) && itemToken(engine) == original,
        "Pause preserves Item token")) return false;
    const auto written = engine.device.written.load(); const auto cursor = T::lane(engine, Lane::ItemBgm)->cursorFrames;
    engine.device.grant(16); std::this_thread::sleep_for(std::chrono::milliseconds{20}); engine.update(20);
    if (!require(engine.device.written.load() == written && T::lane(engine, Lane::ItemBgm)->cursorFrames == cursor,
        "paused native output never reads Item PCM despite device capacity and fake GUI delta")) return false;
    if (!require(resumePlaybackAndMusic(engine)
        && T::waitUntil(engine, [&] { return engine.device.written.load() > written; })
        && T::lane(engine, Lane::ItemBgm)->source == source && itemToken(engine) == original,
        "Resume consumes the same cursor/session")) return false;
    if (!require(engine.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope("playlist", queue({}), ItemId{"item-a"}, itemQueue)})
        && itemToken(engine) == original, "same-Item visual loop does not restart Item BGM")) return false;
    if (!require(engine.processBackgroundMusicControl(PlaybackStopped{}) && !liveBgm(engine), "Stop actually retires Item registration")) return false;
    engine.update(100); std::this_thread::sleep_for(std::chrono::milliseconds{5});
    if (!require(!liveBgm(engine), "later updates do not resurrect stopped source")) return false;
    return require(engine.processBackgroundMusicControl(started) && itemToken(engine) && itemToken(engine) != original,
        "Replay allocates a fresh Item token");
}

bool verifyPlaylistItemCompositionAndScopeReplacement()
{
    Domain::Project project{}; T::AwaitAudioEngine engine;
    if (!prepare(engine, project)) return false;
    const auto playlist = queue({track("playlist-track", fixturePath(), 0.5)}, 0.5);
    const auto item = queue({track("item-a-track", fixturePath(), 0.5)}, 0.4);
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("playlist", playlist, ItemId{"item-a"}, item)}),
        "Playlist and Item are both admitted")) return false;
    const auto originalPlaylist = playlistToken(engine), originalItem = itemToken(engine);
    if (!require(originalPlaylist && originalItem && originalPlaylist != originalItem
        && T::audioState(engine).activeVoices == 2 && stereo(engine, -0.75F * 0.45F, 0.75F * 0.45F),
        "independent queue*track gains sum without ducking in actual A output")) return false;
    const auto itemB = queue({track("item-b-track", fixturePath(), 0.8)}, 0.25);
    if (!require(engine.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope("playlist", playlist, ItemId{"item-b"}, itemB)})
        && playlistToken(engine) == originalPlaylist && itemToken(engine) != originalItem,
        "Item A to B replaces only Item source")) return false;
    const auto tokenB = itemToken(engine);
    if (!require(engine.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope("playlist", playlist, ItemId{"item-a"}, item)})
        && playlistToken(engine) == originalPlaylist && itemToken(engine) != originalItem && itemToken(engine) != tokenB,
        "A to B to A preserves Playlist and freshens each Item incarnation")) return false;
    if (!require(engine.processBackgroundMusicControl(PlaylistMusicConfigChanged{PlaylistId{"playlist"}, queue(playlist.tracks, 0)}),
        "zero Playlist gain is a real applied mutation")) return false;
    const auto before = T::lane(engine, Lane::PlaylistBgm)->cursorFrames;
    if (!require(pull(engine) && T::lane(engine, Lane::PlaylistBgm)->cursorFrames > before
        && playlistToken(engine) == originalPlaylist, "zero gain still consumes Playlist source without restart")) return false;
    if (!require(engine.processBackgroundMusicControl(PlaylistMusicConfigChanged{PlaylistId{"playlist"}, playlist})
        && playlistToken(engine) == originalPlaylist, "restoring gain preserves registration")) return false;
    return require(pull(engine), "restored independent gains produce a real subsequent output block");
}

bool verifyEmbeddedPlaylistItemCompositionAndLifecycle()
{
    Domain::Project project{}; T::AwaitAudioEngine engine;
    if (!prepare(engine, project, true) || !embedded(engine)) return false;
    const auto playlist = queue({track("playlist-track", fixturePath(), 0.25)}, 0.5);
    const auto item = queue({track("item-track", fixturePath(), 0.5)}, 0.25);
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("playlist", playlist, ItemId{"item"}, item)}),
        "three producer lanes are genuinely prepared while paused")) return false;
    const auto embeddedBefore = T::lane(engine, Lane::Embedded)->source;
    const auto playlistBefore = playlistToken(engine), itemBefore = itemToken(engine);
    if (!require(playlistBefore && itemBefore && playlistBefore != itemBefore
        && T::audioState(engine).activeVoices == 3 && resumePlaybackAndMusic(engine), "all three distinct lanes resume")) return false;
    // Streaming F32 normalization is sample/32768; embedded S16 uses the
    // existing asymmetric positive normalization. Assert the exact real sum.
    if (!require(stereo(engine, -0.75F * 1.25F, 24576.0F / 32767.0F + 0.75F * 0.25F),
        "Embedded/Playlist/Item coexist in the same physical mixer path")) return false;
    engine.setPlaybackSpeed(2);
    if (!require(engine.playbackSpeed() == 1 && T::lane(engine, Lane::Embedded)->source == embeddedBefore
        && playlistToken(engine) == playlistBefore && itemToken(engine) == itemBefore,
        "unsupported short-file tempo preparation preserves old actual rate/immutable asset and both BGM sources")) return false;
    if (!require(pausePlaybackAndMusic(engine), "three-source pause applies")) return false;
    const auto written = engine.device.written.load(); engine.device.grant(1);
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    if (!require(engine.device.written.load() == written, "paused three-source mix does not pull PCM")) return false;
    if (!require(resumePlaybackAndMusic(engine)
        && T::waitUntil(engine, [&] { return engine.device.written.load() > written; })
        && playlistToken(engine) == playlistBefore && itemToken(engine) == itemBefore,
        "three-source Resume preserves source identity")) return false;
    if (!require(engine.processBackgroundMusicControl(PlaybackStopped{}) && T::audioState(engine).activeVoices == 0
        && !T::lane(engine, Lane::Embedded) && !liveBgm(engine), "overall Stop retires all three owned lanes")) return false;
    engine.update(100);
    return require(T::audioState(engine).activeVoices == 0, "stopped three-source state has no delayed replay");
}

bool verifyRealExecutorInvalidPathsAndLiveEdits()
{
    const auto missing = fixturePath().parent_path() / "cp1-known-missing-track.wav";
    if (!require(!std::filesystem::exists(missing), "missing-file test precondition is real")) return false;
    T::AwaitAudioEngine engine;
    auto config = queue({track("missing", missing), track("valid")});
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("playlist", config)})
        && playlistSession(engine)->phase() == Phase::Playing && engine.currentPlaylistBackgroundMusicTrackId()->value == "valid",
        "real missing file falls through to prepared valid source")) return false;
    const auto original = playlistToken(engine);
    config = queue({track("replacement")});
    if (!require(engine.processBackgroundMusicControl(PlaylistMusicConfigChanged{PlaylistId{"playlist"}, config})
        && playlistToken(engine) != original && engine.currentPlaylistBackgroundMusicTrackId()->value == "replacement",
        "removing active track starts the configured successor with fresh token")) return false;
    if (!engine.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope("playlist", config, ItemId{"item"}, queue({track("item")}))})) return false;
    const auto item = itemToken(engine); const auto opens = engine.device.openCalls.load(), configures = engine.device.configureCalls.load();
    if (!require(engine.processBackgroundMusicControl(PlaylistMusicConfigChanged{PlaylistId{"playlist"}, queue({})})
        && playlistSession(engine)->phase() == Phase::SilentEmptyQueue && itemToken(engine) == item && liveBgm(engine)
        && engine.device.openCalls.load() == opens && engine.device.configureCalls.load() == configures,
        "removing final Playlist track preserves Item output without device churn")) return false;
    if (!require(engine.processBackgroundMusicControl(ItemMusicConfigChanged{PlaylistId{"playlist"}, ItemId{"item"}, queue({})})
        && !liveBgm(engine) && T::audioState(engine).activeVoices == 0, "removing final Item track leaves no shared input")) return false;
    if (!require(engine.processBackgroundMusicControl(PlaylistMusicConfigChanged{PlaylistId{"playlist"}, queue({track("revived")})})
        && playlistToken(engine) && playlistToken(engine) != original, "active empty queue revives with fresh token")) return false;
    T::AwaitAudioEngine invalid;
    if (!require(invalid.processBackgroundMusicControl(PlaybackSessionStarted{scope("invalid", queue({track("a", missing), track("b", missing)}))})
        && playlistSession(invalid)->phase() == Phase::NoPlayableTrack && !liveBgm(invalid)
        && invalid.device.openCalls.load() == 0, "all-missing queue terminates boundedly without opening output")) return false;
    const auto corrupt = std::filesystem::temp_directory_path()
        / ("DSE_CP1_CorruptAudio_" + std::to_string(Audio::audioMonotonicNow()) + ".bin");
    // A task-owned path; the scope removes only this exact file after all P ACKs.
    if (std::filesystem::exists(corrupt)) return require(false, "corrupt fixture path must be unused");
    { std::ofstream file(corrupt, std::ios::binary); file << "not-an-audio-file"; }
    bool success;
    {
        T::AwaitAudioEngine corruptEngine;
        success = require(corruptEngine.processBackgroundMusicControl(PlaybackSessionStarted{scope("corrupt", queue({track("corrupt", corrupt), track("valid")}))})
            && playlistSession(corruptEngine)->phase() == Phase::Playing
            && corruptEngine.currentPlaylistBackgroundMusicTrackId()->value == "valid" && liveBgm(corruptEngine),
            "real corrupt bytes are classified and fall through to valid track");
    }
    std::error_code error; std::filesystem::remove(corrupt, error);
    return success && require(!error, "owned corrupt fixture is removed after actual retirement");
}

bool verifyPausedLiveEditAndOutputRecoveryWithTwoScopes()
{
    Domain::Project project{}; T::AwaitAudioEngine engine;
    if (!prepare(engine, project)) return false;
    const auto playlist = queue({track("playlist")}), item = queue({track("item")});
    if (!engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("playlist", playlist, ItemId{"item"}, item)})) return false;
    const auto p = playlistToken(engine), i = itemToken(engine);
    if (!pausePlaybackAndMusic(engine)) return false;
    const auto written = engine.device.written.load();
    if (!require(engine.processBackgroundMusicControl(ItemMusicConfigChanged{PlaylistId{"playlist"}, ItemId{"item"}, queue({track("replacement", fixturePath(), 0.75)}, 0.5)})
        && itemToken(engine) != i && playlistToken(engine) == p, "paused Item edit replaces only Item semantic/source identity")) return false;
    engine.device.grant(16); std::this_thread::sleep_for(std::chrono::milliseconds{20});
    if (!require(engine.device.written.load() == written, "paused live edit never consumes new source before Resume")) return false;
    if (!require(resumePlaybackAndMusic(engine)
        && T::waitUntil(engine, [&] { return engine.device.written.load() > written; }), "Resume reads actual edited source")) return false;
    const auto edited = itemToken(engine); const auto opens = engine.device.openCalls.load();
    engine.device.endpoint.fetch_add(1);
    if (!require(T::waitUntil(engine, [&] { return engine.device.openCalls.load() == opens + 1 && engine.device.configured.load() && T::audioState(engine).health == Health::Healthy; })
        && playlistToken(engine) == p && itemToken(engine) == edited, "endpoint reroute preserves both source tokens")) return false;
    engine.device.availabilityResult.store(Audio::AudioBlockStatus::DeviceFailure);
    if (!T::waitUntil(engine, [&] { return T::audioState(engine).health == Health::DeviceUnavailable; })) return false;
    return require(T::waitUntil(engine, [&] { return T::audioState(engine).health == Health::Healthy; })
        && playlistToken(engine) == p && itemToken(engine) == edited && liveBgm(engine)
        && engine.device.openCalls.load() == opens + 2, "actual native-cooldown recovery retains both sources");
}

bool verifyEndedThenMissingSwitchDoesNotDeadlockTwoSourceRecords()
{
    const auto missing=fixturePath().parent_path()/"cp1-liveness-known-missing.wav";
    if (!require(!std::filesystem::exists(missing), "Liveness fixture has a genuinely missing successor")) return false;
    T::AwaitAudioEngine engine;
    const auto config=queue({track("old-a"),track("missing-b",missing),track("fallback-c")});
    if (!require(engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("liveness",config)})
        && playlistSession(engine)->phase()==Phase::Playing, "Liveness fixture starts real prepared source A")) return false;
    auto& control=Access::control(engine);
    const auto old=*T::lane(engine,Lane::PlaylistBgm);
    const auto oldToken=playlistToken(engine);
    engine.pollAudioExecution();
    const auto operation=engine.beginAudioEffect(Engine::AudioEffectScope::Structural);
    if (!require(operation.has_value(), "Mutation is admitted before the concurrent native EOF")) return false;
    // The device consumes the real complete1003-frame source after C's pre-poll
    // but before its admitted mutation. No Engine poll or C mixer pull occurs.
    engine.device.grant(1004);
    const auto rawDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    bool ended=false;
    do {
        const auto observed=control.execution.sourceObservation(old.source);
        ended=observed && observed->terminal==Audio::AudioSourceTerminal::Ended && observed->consumerQuiesced;
        if (!ended) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (!ended && std::chrono::steady_clock::now()<rawDeadline);
    if (!require(ended && playlistToken(engine)==oldToken,
        "A has durable genuine EOF while C still selects the old token")) return false;
    const auto switched=Core::BackgroundMusicEngineTestAccess::processOriginalOrchestrator(engine,
        Music::PlaylistMusicTrackSwitchRequested{Music::PlaylistId{"liveness"},Music::TrackId{"missing-b"}});
    (void)engine.finishAudioEffect(*operation, switched==Music::MusicProcessResult::Completed);
    const auto failedToken=playlistToken(engine);
    Audio::AudioSourceKey failedKey{};
    for (const auto& source:control.sources)
        if (source.occupied && source.key.lane==Lane::PlaylistBgm && source.key!=old.source) failedKey=source.key;
    if (!require(failedToken && failedToken!=oldToken && failedKey.valid(),
        "Original orchestrator switches the admitted operation to a distinct missing-B token")) return false;
    bool producerFailed=false;
    const auto producerDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    do {
        producerFailed=control.preparation.snapshot(failedKey).state==Audio::AudioPreparationState::Failed;
        if (!producerFailed)std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (!producerFailed && std::chrono::steady_clock::now()<producerDeadline);
    const auto occupied=[&] {std::size_t count=0;for(const auto& source:control.sources)
        if(source.occupied&&source.key.lane==Lane::PlaylistBgm)++count;return count;};
    if (!require(producerFailed && occupied()==2,
        "Old terminal and new genuine P failure coexist in the two fixed C records before harvest")) return false;
    bool recovered=false;std::size_t polls=0;
    for (;polls<4096&&!recovered;++polls) {
        engine.pollAudioExecution();
        const auto current=engine.currentPlaylistBackgroundMusicTrackId();
        recovered=current && current->value=="fallback-c" && playlistSession(engine)->phase()==Phase::Playing
            && occupied()==1 && T::lane(engine,Lane::PlaylistBgm).has_value();
        if(!recovered)std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    if (!require(recovered && playlistToken(engine)!=oldToken && playlistToken(engine)!=failedToken,
        "Finite C polling frees stale terminal storage and genuinely prepares fallback C without permanent two-record Busy")) return false;
    const auto fallback=playlistToken(engine);
    for(unsigned poll=0;poll<10;++poll)engine.pollAudioExecution();
    if (!require(playlistToken(engine)==fallback && engine.currentPlaylistBackgroundMusicTrackId()->value=="fallback-c"
        && T::settle(engine), "Old-token EOF never acts as the new Current or restarts the fallback")) return false;
    const auto next=engine.beginAudioEffect(Engine::AudioEffectScope::Structural);
    if (!require(next.has_value(), "Normal structural admission recovers after both retiring records drain")) return false;
    (void)engine.finishAudioEffect(*next);
    std::cout << "EOF/switch/missing-source two-record liveness: PASS polls=" << polls << '\n';
    return require(T::settle(engine), "Post-regression operation reaches a durable terminal outcome");
}

bool verifyVisualDetachAndBindingPreserveBackgroundMusicLifetime()
{
    Domain::Project firstProject{},reboundProject{};T::AwaitAudioEngine engine;
    if (!prepare(engine,firstProject)
        || !engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("visual-rebind",queue({track("surviving-bgm")}))})
        || !pull(engine,16)) return false;
    auto& control=Access::control(engine);
    const auto before=*T::lane(engine,Lane::PlaylistBgm);
    const auto sourceToken=playlistToken(engine);
    const auto playbackLifetime=control.playbackLifetime;
    const auto stopsBeforeDetach=engine.device.stopCalls.load();
    engine.detachProject(); // Exact public production visual-detach operation.
    if (!require(T::settle(engine) && T::lane(engine,Lane::PlaylistBgm)
        && T::lane(engine,Lane::PlaylistBgm)->source==before.source && playlistToken(engine)==sourceToken
        && control.playbackLifetime==playbackLifetime,
        "Visual detach preserves the independent BGM registration, token and Session playback lifetime")) return false;
    if (!prepare(engine,reboundProject)) return false; // Same authorized C Bind path, no policy restart.
    if (!require(T::lane(engine,Lane::PlaylistBgm) && T::lane(engine,Lane::PlaylistBgm)->source==before.source
        && playlistToken(engine)==sourceToken && pull(engine,16)
        && T::lane(engine,Lane::PlaylistBgm)->cursorFrames>before.cursorFrames
        && engine.device.stopCalls.load()==stopsBeforeDetach,
        "BGM-only visual rebind consumes the same source without replacing its identity or flushing the shared queue")) return false;
    return require(engine.processBackgroundMusicControl(PlaybackStopped{}) && !T::lane(engine,Lane::PlaylistBgm)
        && T::audioState(engine).activeVoices==0,
        "Explicit PlaybackStopped still retires the surviving BGM and all owned physical lanes");
}

bool verifyDeterministicLifecycleSoak()
{
    constexpr std::size_t CycleCount = 300;
    Domain::Project project{}; T::AwaitAudioEngine engine;
    if (!prepare(engine, project)) return false;
    const auto playlist = queue({track("playlist-a"), track("playlist-b")});
    const auto itemA = queue({track("item-a-1"), track("item-a-2")});
    const auto itemB = queue({track("item-b-1"), track("item-b-2")});
    if (!engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("soak", playlist, ItemId{"item-a"}, itemA)})) return false;
    std::optional<Music::MusicSourceToken> previousPlaylist, previousItem;
    std::set<std::uint64_t> retired;
    const auto observe = [&] {
        const auto check = [&](auto current, auto& previous) {
            if (!current || current->value == 0 || (current != previous && retired.contains(current->value))) return false;
            if (previous && current != previous && !retired.insert(previous->value).second) return false;
            previous = current; return true;
        };
        return check(playlistToken(engine), previousPlaylist) && check(itemToken(engine), previousItem)
            && playlistToken(engine) != itemToken(engine);
    };
    if (!require(observe(), "soak starts with distinct nonzero source tokens")) return false;
    std::size_t acceptedPlaybackRestarts = 0;
    for (std::size_t cycle = 0; cycle < CycleCount; ++cycle)
    {
        const auto id = ItemId{cycle % 2 ? "item-b" : "item-a"}; const auto& active = cycle % 2 ? itemB : itemA;
        bool ok = true;
        switch (cycle % 10)
        {
        case 0: ok = engine.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope("soak", playlist, id, active)}); break;
        case 1: ok = engine.processBackgroundMusicControl(PlaylistMusicConfigChanged{PlaylistId{"soak"}, queue(playlist.tracks, cycle % 4 == 1 ? 0 : 0.75)}); break;
        case 2: ok = engine.processBackgroundMusicControl(ItemMusicConfigChanged{PlaylistId{"soak"}, id, queue(active.tracks, cycle % 4 == 2 ? 0 : 0.6)}); break;
        case 3: ok = pausePlaybackAndMusic(engine); break;
        case 4: ok = resumePlaybackAndMusic(engine); break;
        case 5: { const auto opens = engine.device.openCalls.load(); engine.device.endpoint.fetch_add(1);
            ok = T::waitUntil(engine, [&] { return engine.device.openCalls.load() == opens + 1 && engine.device.configured.load() && T::audioState(engine).health == Health::Healthy; }); break; }
        case 6: engine.device.availabilityResult.store(Audio::AudioBlockStatus::DeviceFailure);
            ok = T::waitUntil(engine, [&] { return T::audioState(engine).health == Health::DeviceUnavailable; })
                && T::waitUntil(engine, [&] { return T::audioState(engine).health == Health::Healthy; }); break;
        case 7: ok = engine.processBackgroundMusicControl(PlaybackStopped{})
            && engine.processBackgroundMusicControl(PlaybackSessionStarted{scope("soak", playlist, id, active)}); break;
        case 8: ok = engine.processBackgroundMusicControl(PlaylistMusicConfigChanged{PlaylistId{"soak"}, queue(playlist.tracks, 1, QueueMode::Shuffle)}); break;
        case 9: ok = engine.processBackgroundMusicControl(ActivePlaybackScopeChanged{scope("soak", playlist, id, active)}); break;
        }
        if (cycle % 10 == 7 && ok) ++acceptedPlaybackRestarts;
        if (ok && !engine.isPaused()) ok = pull(engine, 4);
        const bool tokensValid = ok && observe();
        const auto audio = T::audioState(engine);
        const auto bytes = Access::control(engine).preparation.reservedPcmBytes();
        const bool invariant = tokensValid && audio.activeVoices == 2 && audio.health == Health::Healthy
            && bytes <= Audio::AudioSourcePreparation::MaximumEmbeddedPcmBytes;
        if (!invariant)
        {
            const auto& control = Access::control(engine);
            std::cerr << "Soak diagnostic cycle=" << cycle << " category=" << cycle % 10
                << " action-ok=" << ok << " tokens-valid=" << tokensValid
                << " voices=" << audio.activeVoices << " health=" << static_cast<unsigned>(audio.health)
                << " transport=" << static_cast<unsigned>(audio.transport) << " paused=" << engine.isPaused()
                << " playlist-token=" << (playlistToken(engine) ? playlistToken(engine)->value : 0)
                << " item-token=" << (itemToken(engine) ? itemToken(engine)->value : 0)
                << " internal=" << control.internalCount << " reserved-pcm=" << bytes << '\n';
            for (const auto& source : control.sources) if (source.occupied)
                std::cerr << "  source lane=" << static_cast<unsigned>(source.key.lane)
                    << " token=" << source.key.sourceToken << " retiring=" << source.retiring
                    << " submitted=" << source.submitted << " started=" << source.startedDelivered
                    << " producer-state=" << static_cast<unsigned>(control.preparation.snapshot(source.key).state) << '\n';
        }
        if (!require(invariant,
            "300-cycle soak keeps unique tokens, bounded records, two live sources, and healthy A output")) return false;
    }
    const auto finalAudio = T::audioState(engine);
    std::cerr << "Soak work diagnostic opens=" << engine.device.openCalls.load()
        << " configures=" << engine.device.configureCalls.load()
        << " writes=" << engine.device.writeCalls.load()
        << " credits=" << engine.device.credits.load()
        << " written=" << engine.device.written.load()
        << " health=" << static_cast<unsigned>(finalAudio.health)
        << " failures=" << finalAudio.deviceFailures
        << " recovery-attempts=" << finalAudio.recoveryAttempts
        << " recovered=" << finalAudio.recoveredCount << '\n';
    const auto expectedEventsPerCategory = CycleCount / 10;
    if (!require(acceptedPlaybackRestarts == expectedEventsPerCategory,
        "Soak completes exactly30 successful Stop/Start pairs")
        || !require(finalAudio.deviceFailures == expectedEventsPerCategory
            && finalAudio.recoveryAttempts == expectedEventsPerCategory * 2
            && finalAudio.recoveredCount == finalAudio.recoveryAttempts,
            "The controlled30 outages and30 endpoint changes each recover once without extra failures")) return false;
    // Stop/Start resets and reconfigures the retained healthy client; it need
    // not reopen it. Only those successful explicit restart pairs add a valid
    // configure allowance beyond the ordinary one configure per open.
    if (!require(engine.device.openCalls.load() < CycleCount
        && engine.device.configureCalls.load() <= engine.device.openCalls.load() + acceptedPlaybackRestarts
        && engine.device.writeCalls.load() < CycleCount * 4, "soak has bounded reopen/configure/refill work")) return false;
    if (!require(engine.processBackgroundMusicControl(PlaybackStopped{}) && !liveBgm(engine), "soak cleanup truly retires both sources")) return false;
    engine.update(100);
    return require(T::audioState(engine).activeVoices == 0, "soak cleanup cannot replay a delayed source");
}
}

int main()
{
    const bool passed = verifyItemLifecycleAndSameItemLoop()
        && verifyPlaylistItemCompositionAndScopeReplacement()
        && verifyEmbeddedPlaylistItemCompositionAndLifecycle()
        && verifyRealExecutorInvalidPathsAndLiveEdits()
        && verifyPausedLiveEditAndOutputRecoveryWithTwoScopes()
        && verifyEndedThenMissingSwitchDoesNotDeadlockTwoSourceRecords()
        && verifyVisualDetachAndBindingPreserveBackgroundMusicLifetime()
        && verifyDeterministicLifecycleSoak();
    if (passed) std::cout << "BackgroundMusicFinalIntegrationTest CP1 PASS (six mapped groups + two focused lifecycle regressions; 300 cycles)\n";
    return passed ? 0 : 1;
}
