# ADR-0006: Background Music Architecture and Playback Semantics

Status: Accepted product contract — implementation deferred

Date: 2026-08-25

Related Documents:

- `CONSTITUTION.md`
- `01_ARCHITECTURE.md`
- `02_DATA_MODEL.md`
- `04_JSON_SPECIFICATION.md`
- `05_ROADMAP.md`
- `GLOSSARY.md`
- `ADR/0001-Architecture.md`
- `ADR/0004-Infrastructure-Serialization-Boundary.md`

---

# 1. Authority and purpose

This document defines **Background Music (BGM) V1** product semantics and architecture principles for DesktopSceneEngine. It is normative for future BGM implementation planning.

Its audience is maintainers who will design BGM configuration, runtime, resource, mixer, and output work. Its success condition is that such work can be planned and reviewed without reconstructing this decision from current code or visual-playback behavior.

Current `AudioResource`, `AudioOutput`, Engine, and `PlaybackRuntime` implementations do **not** override this contract. Later implementation surveys MUST adapt code toward these semantics rather than silently redefine the product model to match current implementation details.

This ADR freezes product and ownership decisions only. It introduces no API, data-model schema, runtime class, persistence migration, mixer, or configuration UI implementation.

# 2. Product scope

Background Music V1 has two independent music scopes within a Playlist. Both scopes may be active and audible at the same time.

| Scope | Owner | Configuration | Runtime lifetime |
| --- | --- | --- | --- |
| **Playlist Global BGM** | one Playlist | zero or more tracks, music queue mode, independent volume | while that Playlist identity remains active |
| **Item BGM** | one Item in a Playlist | zero or more tracks, music queue mode, independent volume | while that Item identity remains active |

Playlist Global BGM and Item BGM are additive. They MUST NOT be mutually exclusive and MUST NOT automatically override, pause, duck, or suppress each other. They MAY play simultaneously with each other and with video embedded audio. Their current track, queue state, failure state, and volume are independent.

# 3. Audio source compatibility tiers

| Tier | Formats | Product commitment |
| --- | --- | --- |
| **Tier 1 — primary** | MP3, AAC/M4A, FLAC, WAV, OGG Vorbis, Opus | Officially supported and the primary validation set. |
| **Tier 2 — best effort** | WMA, ALAC, AIFF/AIF, APE | Compatibility-oriented support without the Tier 1 guarantee. |

Actual usability MUST ultimately be determined by decoder/probe capability, not filename extension alone. MIDI is outside ordinary waveform playback and is not part of BGM V1. Tracker/module formats and movie-oriented AC3, EAC3, and DTS are also outside the V1 BGM target.

# 4. Core architecture principle

Background Music is an independent playback subsystem. It does **not** follow the visual timeline clock and does not require video `currentTime`, image elapsed time, visual Item duration, Canvas state, Renderer state, or frame timing.

```text
Main Playback Runtime
        |
        | lifecycle / active identity
        v
Background Music Runtime
```

The Background Music Runtime owns its current track, decoder state, playback position, queue progression, shuffle state, failure handling, and volume.

## 4.1 Control plane and audio data plane

The following relationships are separate by design:

```text
Control plane

Main Playback
    ↓ lifecycle / active Playlist / active Item
Background Music Runtime
```

```text
Audio data plane

Video embedded PCM     Playlist BGM PCM     Item BGM PCM
        \                   |                   /
         \                  |                  /
                    future mixer
                         ↓
                 AudioOutput / WASAPI
```

Shared eventual output MUST NOT couple BGM runtime state to visual timing. A future mixer is data-plane convergence only; it is not an authority over BGM queue policy or visual playback policy.

## 4.2 Communication constraint

Main playback conceptually reports lifecycle facts such as:

- `PlaybackSessionStarted(activePlaylist, activeItem)`;
- `PlaybackPaused`;
- `PlaybackResumed`;
- `PlaybackStopped`;
- `ActivePlaylistChanged`; and
- `ActiveItemChanged`.

The music subsystem determines the resulting music actions. Main playback MUST NOT prescribe a particular music track, queue advance, BGM seek, or shuffle reset. This is semantic one-way coupling, not a requirement for a generic EventBus. A later implementation may use explicit controller calls, narrow interfaces, direct lifecycle methods, or another bounded mechanism that preserves this rule.

# 5. Ownership and state boundaries

## 5.1 Configuration model versus runtime state

The Project/configuration model owns persistent music intent:

- track references and configured ordering;
- queue playback mode;
- Playlist Global BGM Queue Volume and Item BGM Queue Volume;
- independent MusicTrack Volume for each queue entry;
- Playlist visual Item sequence mode; and
- future persistent settings only when separately defined.

Runtime owns transient execution state:

- current track;
- decoder and playback cursor;
- shuffle permutation/bag;
- currently failed tracks and diagnostics;
- paused state; and
- transient queue progression.

Runtime state MUST NOT be persisted into the Project merely because it changes during playback. A shuffle permutation is runtime state, not Project configuration. BGM persistence or JSON migration is not authorized by this ADR and remains a later explicit decision.

## 5.2 Four separate sequence and policy owners

Similar-looking controls may govern different owners. They MUST remain distinct.

| Policy or sequence | Semantic owner | Controls |
| --- | --- | --- |
| Playlist Visual Item Sequence | Playlist | visual Item progression within the Playlist |
| Playlist Global BGM Queue Sequence | Playlist Global BGM scope | Playlist-level music tracks |
| Item-local owned playback sequence | Item, where the current product model defines one | Item-local media behavior; exact future terminology remains to be clarified |
| Item BGM Queue Sequence | Item BGM scope | music tracks owned by that Item |

The third row is intentionally not expanded into an invented implementation model. Its ambiguity MUST be resolved by the future technical/product survey before it is represented in UI or persistence.

# 6. Identity-based lifecycle

Playback continuity follows **active identity**, not navigation history and not the cause of navigation. Relevant identities are the active Playlist identity and active Item identity.

If the relevant identity remains the same, its music session continues. If it changes, the old scoped runtime stops and the newly active scope starts fresh from its configuration. Automatic and manual Item changes are intentionally equivalent for this purpose.

## 6.1 Playlist Global BGM lifetime

Playlist Global BGM belongs to the active Playlist scope. While the active Playlist identity is unchanged, Item changes, visual Item loops, visual sequence wrapping, and visual shuffle selection MUST NOT restart it. It changes only when Playlist identity changes or the overall session is paused/stopped.

## 6.2 Item BGM lifetime

Item BGM belongs to active Item identity. A visual duration loop or repeated rendering of the same Item does not reactivate Item BGM. When an Item identity changes, previous Item BGM stops and the new Item BGM starts fresh.

For `Item 1 → Item 2 → Item 1`, both visits to Item 1 are fresh Item BGM sessions. V1 has no history-based Item BGM resume behavior.

## 6.3 Playlist switch

On `Playlist A → Playlist B`:

1. Playlist A BGM stops.
2. The active Item BGM in Playlist A stops.
3. Playlist B Global BGM starts fresh.
4. Playlist B's active Item BGM starts fresh.

Returning to Playlist A later creates a new Playlist A music session. Its former track position, track runtime state, shuffle bag, and Item BGM position are not preserved.

# 7. Pause, Stop, and visual loops

## 7.1 Pause and Resume

Main Player Pause pauses all active media resources: visual runtime, video embedded audio, Playlist Global BGM, and Item BGM. It preserves active identities, current tracks, music positions, practical decoder/session state, and shuffle state. Resume continues that existing state.

Pause MUST NOT be modeled semantically as Stop followed by Start.

## 7.2 Stop and later Start

Stop ends the current playback session and destroys or resets visual session state, both BGM runtime sessions, current tracks, playback positions, and shuffle state. Play after Stop creates a fresh session.

## 7.3 Normative lifecycle table

| Event | Visual behavior | Playlist Global BGM | Item BGM |
| --- | --- | --- | --- |
| Start Playlist A | start its active Item | start fresh | start active Item scope fresh |
| Pause | pause | pause, preserving session | pause, preserving session |
| Resume | resume | resume | resume |
| Same-Item visual loop | loop visual content | continue | continue |
| `Item 1 → Item 2` (automatic or manual) | activate Item 2 | continue | stop Item 1; start Item 2 fresh |
| Playlist visual sequence wraps last → first | wrap visual sequence | continue | stop old Item; start new active Item fresh |
| `Playlist A → Playlist B` | activate B's Item | stop A; start B fresh | stop A Item; start B Item fresh |
| Stop | stop/reset session | stop/reset | stop/reset |

# 8. Music queues

Each Playlist Global BGM queue and Item BGM queue supports one shared semantic model:

- **Current Single Loop:** current music track repeats indefinitely.
- **List Loop:** configured tracks proceed in order and wrap after the last track.
- **Shuffle:** a shuffle-bag/permutation determines order.

Playlist visual Item sequence mode uses the same names but controls visual Item progression only. It MUST NOT control Playlist Global BGM order.

## 8.1 Empty queues and invalid tracks

An empty Playlist or Item BGM queue is valid. It produces no BGM for that scope, without fatal error or blocking visual playback or the other BGM scope.

If a track is missing, unsupported, corrupt, or undecodable, its runtime failure is reported/recorded transiently, the track is skipped, and the queue advances under its current policy. The failure MUST NOT be permanently encoded in the Project model: restoring a source later may make it usable.

Within one progression attempt, distinct track attempts MUST be bounded by queue size. The runtime MUST NOT loop forever through invalid tracks. If none is playable, that scope enters a silent `NoPlayableTrack` runtime state while visual playback and the other BGM scope continue.

## 8.2 Shuffle semantics

Shuffle uses a permutation of available queue tracks, not independent random picks. A shuffle round avoids repetition until exhausted, then reshuffles. When more than one track exists, it SHOULD avoid using the previous round's final track as the next round's first track where practical.

Pause/Resume preserves shuffle state. Stop or scope deactivation destroys it, and fresh activation creates a new shuffle state.

# 9. Live configuration

## 9.1 Configuration window

BGM configuration SHOULD enter through a Playlist context menu:

```text
Playlist context menu
        ↓
Configure Background Music…
```

It opens a Playlist-scoped, modeless, top-level/peer-style configuration window. The window edits that Playlist's Global BGM queue, its Items' BGM queues, queue modes, and independent volumes. It MUST NOT accidentally edit another Playlist.

Different Playlist windows may coexist. Reopening a Playlist's configuration SHOULD focus and raise the existing window for that Playlist identity rather than creating a duplicate editor. The workflow MUST NOT be modal or block interaction with the main Player.

## 9.2 Live-edit rule

V1 configuration is live edit. It has no whole-window Apply transaction, shadow copy, or OK/Cancel commit model. Changes mutate Playlist-owned music configuration immediately. If a changed scope is active, its runtime synchronizes according to the following rules.

| Configuration change | Required live behavior |
| --- | --- |
| Volume | Apply immediately without restarting the current track. |
| Add track | Do not interrupt current track; add it to future progression. In Shuffle, add it deterministically to remaining future pool. |
| Remove non-current track | Do not interrupt current playback; remove it from future sequence/shuffle progression. |
| Remove current track | Stop it immediately and advance from updated queue under current mode. |
| Clear queue | Stop that scope, enter silence, and leave visual playback and other BGM scope unchanged. |
| Reorder queue | Do not interrupt current track; use new ordering for later progression. |
| Change queue mode | Do not restart current track; apply new mode after current track completes. |

In Shuffle mode, adding adds to remaining pool; removing an unplayed/non-current track removes it from that pool; removing the current track stops it and selects from remaining valid shuffled candidates; removing an already-played track needs no retroactive change. Runtime state SHOULD change only as much as required to honor modified configuration.

Examples of deferred mode effect: List Loop → Current Single Loop repeats current track after it finishes; Current Single Loop → List Loop follows current configured successor; List Loop → Shuffle initializes future shuffle state while avoiding an immediate repeat of current track where practical.

# 10. Volume, failure, and freedom principles

Each BGM scope has two configuration levels. The effective per-track configuration gain for
Playlist Global BGM is:

```text
Playlist Global BGM Queue Volume × individual MusicTrack Volume
```

The effective per-track configuration gain for Item BGM is:

```text
Item BGM Queue Volume × individual MusicTrack Volume
```

Queue Volume remains independently configurable for every Playlist Global BGM and Item BGM
queue. Each MusicTrack queue entry owns its own Track Volume, which defaults to `1.0` (100%).
Track Volume is configuration/model state, not runtime state. Duplicate source paths remain
independent queue entries by TrackId and may therefore have different Track Volumes.

These values do not define current runtime mixing or final post-mix/master-output semantics.
A future mixer/runtime may consume them under a separately authorized implementation. There is
no new BGM-wide master volume in V1, and the final relationship to existing Player
output/master controls is not hard-wired here.

Playlist BGM, Item BGM, and visual playback have independent failure boundaries. A BGM decode or output failure MUST NOT stop visual playback, Playlist Item progression, Canvas rendering, or video playback. It may expose diagnostics to UI.

V1 gives users predictable primitives and resource freedom instead of hidden conflict resolution. It does not synchronize BGM duration with video/image duration or automatically resolve every audiovisual-duration conflict. When an automatic policy would be ambiguous or intrusive, V1 prefers a simple deterministic rule.

# 11. Conceptual runtime model

This diagram describes ownership only. It does not prescribe exact class names.

```text
Playback Session
│
├── Visual / Media Runtime
│
└── Background Music Runtime
    │
    ├── Playlist-scope Music Session
    │   ├── queue and mode
    │   ├── volume
    │   ├── current track
    │   └── shuffle and other runtime state
    │
    └── Item-scope Music Session
        ├── queue and mode
        ├── volume
        ├── current track
        └── shuffle and other runtime state
```

# 12. V1 non-goals

The following are deliberately deferred:

- crossfade, automatic ducking, automatic Playlist-versus-Item override, and fade-in/fade-out automation;
- gapless-playback guarantees;
- resuming Playlist or Item BGM position after scope deactivation/reactivation;
- BGM synchronization to video/image duration, beat synchronization, BPM analysis, waveform editing, or music timeline editing;
- DSP effects/equalizer, MIDI synthesis, and generalized audio-routing UI;
- persistent cache lifecycle work; and
- JSON/project migration unless separately authorized.

# 13. Future implementation survey

Before implementation, a technical survey MUST answer:

1. Can existing `AudioResource` decode Tier 1 standalone audio cleanly?
2. Is current `AudioOutput` fundamentally single-stream?
3. Where should multi-stream PCM mixing live?
4. Can embedded video audio remain unchanged while two independent BGM streams are introduced?
5. What owns BGM decoder and session lifetime?
6. What narrow lifecycle seam connects main playback to BGM runtime?
7. How can Playlist BGM and Item BGM fail independently?
8. What minimal model changes are needed before JSON vNext?
9. Which dependency-first implementation slice should come first: standalone audio decode/resource, music queue/session, multi-stream mixer, or another proved prerequisite?
10. How will all of this preserve current Engine, Runtime, Resources, and Platform dependency boundaries?

# 14. Implementation acceptance criteria

Future implementation planning is acceptable only when it preserves all of the following:

- Playlist and Item BGM coexist without implicit override.
- BGM reacts to lifecycle and identity changes, not `PlaybackRuntime::currentTime()`.
- Pause resumes existing BGM sessions while Stop later creates fresh sessions.
- Item BGM restarts on Item identity change, not on a visual loop.
- Playlist Global BGM continues across Item changes within same Playlist.
- Queues use specified loop and shuffle-bag semantics, including bounded invalid-track handling.
- Live configuration has required non-restart behavior except when current track is removed or its queue is cleared.
- BGM failure cannot back-propagate into visual playback.

# 15. Status

This ADR is the authoritative V1 Background Music product contract. It deliberately precedes implementation survey and does not claim that BGM runtime, configuration UI, multi-stream mixing, persistence, or output integration exists today.
