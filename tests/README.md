# Tests and optional media fixtures

Build on Windows with the prerequisites in the [project README](../README.md). Enable `BUILD_PLAYER=ON` and `BUILD_TESTING=ON` for the full registered suite. A release build of Player does not need any test media.

```powershell
cmake --build $buildDir --config Debug
ctest --test-dir $buildDir -C Debug --output-on-failure -j 1
```

CTest passes checked-in fixture paths as absolute build arguments. Some tests exercise real Windows graphics, audio, foreground focus, dialogs or process lifecycle. Use an interactive desktop for those tests. The default registration includes neither the optional media cases below nor the manual graphics soak.

## Available in a clean clone

| Test or test family | Fixture | Path source |
| --- | --- | --- |
| `AudioResourceTest` | Tracked synthetic MP3. Decoder, PCM, duration and tempo assertions run by default. | CTest passes `TestData/Audio/SequentialAudioSourceCompressed.mp3`. |
| `SequentialAudioSourceTest`, `SequentialAudioMixerInputTest`, `BackgroundMusicExecutorTest`, `BackgroundMusicEngineIntegrationTest`, `BackgroundMusicFinalIntegrationTest`, `AudioOutputRecoveryEngineTest`, `AudioSourcePreparationTest` | Tracked synthetic stereo WAV and, where applicable, FLAC/MP3. | CMake passes the tracked `TestData/Audio/` directory. `SequentialAudioSourceTest.cpp` describes the provenance of the compressed audio. |
| `VideoResourceTest_MissingPath` | Deliberately missing path to verify rejection. | Test code. The positive case is registered separately when media is configured. |
| `RepresentativeFrameStoreTest` | Source bytes and injected deterministic RGBA frames. | Created by the test in its temporary directory. |
| `ActiveProjectionRebuildTest_FixtureIndependent` | Successor indexing and removal using the existing authored test model. | Test code. |
| `EngineSceneNavigationTest_FixtureIndependent` | Empty/timed scenes for navigation, completion and playback-state checks. | Test code. |
| `EngineSeekTest_Generated` | Zero-duration scene and a generated silent AVI for preparation/rate and seek behavior. | Test code and its temporary directory. |
| `TimelinePreviewProviderTest_Generated`, `TimelinePreviewWorkerTest_Generated` | Generated PNG for image behavior. Worker also exercises request replacement, cancellation, preload priority, stale results and real thread shutdown. | Test code and its temporary directory. |
| `EnginePlaybackEofTest` and production shell/Session tests using local media | Generated BMP, PNG, WAV, or AVI as needed. | Produced by the tests or their harnesses in a dedicated path. |

Paths used as synthetic values in model and serialization tests are inputs to assertions. They do not need to exist.

## Opt in to the real-media suite

Set `DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR` to an existing absolute directory outside the published source tree. The tests do not search for a local media folder. With the option empty, configuration reports `External media tests: UNAVAILABLE`. CTest does not register these cases, so the default pass count never includes them. An explicitly supplied missing or incomplete root fails configuration.

```powershell
cmake -S . -B $buildDir "-DDESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR=<absolute-fixture-root>"
cmake --build $buildDir --config Debug
ctest --test-dir $buildDir -C Debug -L external-media --output-on-failure --no-tests=error -j 1
```

Use lowercase extensions and a path without quotes or semicolons. To opt out, reconfigure with `-DDESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR=`. Keep the selected media local and use clips you are allowed to use.

```text
<absolute-fixture-root>/
  Images/     at least two regular JPG/JPEG/PNG files with lowercase extensions
  Videos/     at least two regular .mp4 files
  ...         at least one .png anywhere under the root, including Images/
```

Use decodable media with valid dimensions. The MP4s should have distinct frames with useful keyframe spacing. The first sorted video for seek and the direct video-resource case should be longer than 4 s and have embedded audio. Any video eligible for recursive preview discovery should be decodable at 10 s and have enough decode work for cancellation tests. CMake checks discovery and count. Unsuitable content still fails the real tests. Recursive discovery order is unspecified, so make each eligible clip suitable for the intended assertions.

| Test (`DesktopSceneEngine_` prefix and `_ExternalMedia` suffix) | Fixture / coverage |
| --- | --- |
| `ActiveProjectionRebuildTest` | Sorted-first image and MP4, with a decodable frame at 0.25 s. Actual-media Playlist rebuilding and playback continuity. |
| `EngineSceneNavigationTest` | Two sorted images and MP4s. Alternating image/video navigation and playback-state preservation. |
| `EngineSeekTest` | Sorted-first image and MP4 with embedded audio. Image/video seek, flushing, timing authority and BGM continuity. The BGM track uses the checked-in WAV. |
| `VideoResourceTest` | Sorted-first MP4. Positive decoder, seek, reuse and EOF recovery assertions. |
| `PreviewVideoDecoderTest` | Recursively discovered MP4s. Real decode, sizing, independent decoders, cancellation and recovery. |
| `TimelinePreviewProviderTest` | Recursively discovered PNG and MP4. Real representative frames, persistent cache and subsequent reuse. |
| `TimelinePreviewWorkerTest` | Two distinct recursively discovered MP4s. Real video decode, preload, source switching, cancellation and shutdown. |
| `RepresentativeFrameStoreIntegrationTest` | Recursively discovered MP4 with a usable frame at time zero. Real decode, PNG persistence and RAM reuse. |

`DesktopSceneEngine_PublicFixtureRegistrationTest` checks the actual CMake registration in the default, opted-in, missing-root and incomplete-root states. It uses temporary discovery markers. The native tests own the media decode checks.

## Manual media tools

- Building the Sandbox requires no local media. Its no-argument run searches for local `TestResources/` and needs two images and two MP4s. It is a manual example, outside CTest and release packaging.
- `DesktopSceneEngine_EngineGraphicsSoak` is a manual Release harness with an explicit `--media` argument. `Performance/Measure-Soak.ps1 -SelfTest` exercises only the measurement selectors without media. A canonical run requires `-Media <owned-canonical-JPEG>` and retains the existing size and SHA-256 checks. The canonical fixture is unavailable from a clean clone. A substitute image cannot qualify the canonical run.
