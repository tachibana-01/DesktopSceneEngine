# Tests and optional media fixtures

## Build and run

The main [README](../README.md#tests) describes the Windows/MSVC/Qt/vcpkg build.
Use `BUILD_PLAYER=ON` with `BUILD_TESTING=ON`: the Windows test suite includes
production-Player process and translation consumers. A test build is not
required to build the shipped Player.

CTest normally runs from the tests' binary directory, not `bin/Debug`. Fixture
paths below are passed as absolute CMake arguments/definitions; changing the
working directory does not supply missing media.

Tests range from pure logic to native GPU/audio, foreground/modal, process and
Explorer integration. The full suite is not a headless/locked-desktop contract.
Real desktop tests can create visible presentation windows or briefly deploy
behind icons. Use a dedicated interactive desktop and keep display/power state
stable. Run foreground-sensitive native-modal cases from a foreground terminal
when qualifying them. A skipped environment or absent registration is not a PASS.

## Checked-in deterministic fixtures

`tests/TestData/Audio/` contains a stereo WAV and synthetic FLAC/MP3.
[SequentialAudioSourceTest.cpp](Resources/SequentialAudioSourceTest.cpp) documents
the compressed files' deterministic 48 kHz stereo sample provenance; they contain
no third-party song material.

`DesktopSceneEngine_AudioResourceTest` now receives the tracked
`SequentialAudioSourceCompressed.mp3` instead of an absent private song.
Its decoder, PCM/frame-count, duration and 0.5x/2x/1x tempo assertions are unchanged.

`DesktopSceneEngine_RepresentativeFrameStoreTest` creates temporary source bytes
and injected deterministic RGBA frames itself. Its unused private-fixture
compile definition was removed; its registration and assertions remain intact.

## Explicit external image/video integration suite

No test defaults to the excluded `TestResources/` tree. By default, the nine
targets below are **not built or registered**, with a configure-time status message.
The pure/generated-fixture tests remain available.

To opt in, reconfigure the already configured build from the source root:

```powershell
cmake -S . -B $buildDir -DBUILD_PLAYER=ON -DBUILD_TESTING=ON `
  "-DDESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR=<absolute-fixture-root>"
cmake --build $buildDir --config Debug
ctest --test-dir $buildDir -C Debug --output-on-failure -j 1
```

Keep the fixture root outside the published tree. Do not copy private media into
the repository. To opt out again, pass
`-DDESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR=` and reconfigure.

The opt-in contract is one complete media bundle:

```text
<absolute-fixture-root>/
  Images/       at least two regular .jpg/.jpeg/.png files
  Videos/       at least two regular .mp4 files
  ...           at least one .png anywhere recursively (Images can supply it)
```

Use lowercase extensions. The configured root must exist, be absolute, and contain
no quotes or semicolons. Missing/incomplete explicit bundles cause a configure
error, not an unconditional skip. CMake checks file discovery/counts only; the
real tests still fail on undecodable or semantically unsuitable media.

All target names in the table have the `DesktopSceneEngine_` prefix. Each previously
read the source-root `TestResources/` via a compile definition. The new definition
points only to the explicitly supplied external root; no test assertions changed.

| Target | Required media / preserved intent |
| --- | --- |
| `ActiveProjectionRebuildTest` | Sorted-first image and MP4; decodable video with a frame at 0.25 s. Active/non-active Playlist rebuilding, deletion and playback continuity. |
| `EngineSceneNavigationTest` | Two sorted images and two sorted MP4s, all decodable with positive dimensions. Alternating image/video navigation and state preservation. |
| `EngineSeekTest` | Sorted-first image and MP4; video duration greater than 4 s **with embedded audio**. Seek/flush and BGM continuity; BGM still uses the tracked WAV. |
| `PreviewVideoDecoderTest` | Every recursively discovered lowercase MP4 must decode. The first also exercises concurrent decoders, cancellation checkpoints and recovery; tiny/all-intra media is not assumed equivalent. |
| `TimelinePreviewProviderTest` | First recursively discovered PNG and MP4. Video needs a usable frame at 10 s; requests at 12/14 s map to that representative bucket. Generation, RAM/disk reuse and reopening. |
| `TimelinePreviewWorkerTest` | Two distinct recursively discovered MP4 paths. Real decoding, preload, source switching, obsolete-request cancellation and shutdown. Cancellation remains fixture/scheduling sensitive. |
| `RepresentativeFrameStoreIntegrationTest` | A recursively discovered MP4 decodable at time zero. Real bounded decoder → PNG persistence → RAM reuse. |
| `MainWindowBackgroundMusicForwardingTest` | First discovered lowercase image in Images, decodable for active Engine playback. Music paths are injected control-forwarder inputs, not private song fixtures. |
| `MainWindowPlaybackSequenceTest` | Decodable image in Images and MP4 in Videos, including the default deletion-wrap/guard case. Playback sequencing and active-Item deletion invariants. |

Recursive discovery order is not a stable media-selection contract. Supply only
suitable files, especially for the first-video duration/cancellation requirements;
two arbitrary filenames alone do not qualify a media suite.

## Registration-policy regression

The lightweight check executes the real `tests/CMakeLists.txt` registration logic
while capturing target/CTest declarations instead of constructing binaries:

```powershell
cmake -P tests/cmake/PublicFixtureRegistrationTest.cmake
```

It covers no opt-in, a complete bundle (including multi-dot filenames), nonexistent
root and incomplete root; verifies external definitions, the tracked MP3 argument
and preservation of the generated-frame Store test. Discovery-marker files are
temporary, not real media. This check is also registered as
`DesktopSceneEngine_PublicFixtureRegistrationTest`.

It does **not** replace a full Windows configure/build, decoder acceptance, native
desktop qualification or complete external-media regression run.

## Existing manual-tool limitations

- `DesktopSceneEngine_VideoResourceTest` has no video argument in its default CTest
  registration, so that invocation tests missing-path rejection only. Its existing
  executable can also accept an explicit video path for positive decoder checks.
- Sandbox's no-argument runtime searches for local `TestResources/` and needs two
  images/two MP4s. It is not a clean-clone media smoke test; building it needs no media.
- `DesktopSceneEngine_EngineGraphicsSoak` is `EXCLUDE_FROM_ALL` and is not registered
  in CTest. `Performance/Measure-Soak.ps1` retains a historical, private canonical
  JPEG default with frozen hash/size checks; substituting an arbitrary image is not
  a valid canonical rerun. This source publication does not supply that fixture,
  change its identity, run a soak, or claim portable canonical-soak reproduction.
