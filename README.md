# DesktopSceneEngine

DesktopSceneEngine is a Windows desktop scene player written in C++20, with a
Qt 6 Widgets interface and a native D3D11/DXGI/DirectComposition rendering backend.
It lets a display-bound session edit and preview image/video playlists, then deploy
the same playback onto that display's desktop behind the icons.

## Development status

This is an actively developed application, not a claim of universal Windows-shell
compatibility or a completed long-term roadmap. The current source includes:

- Image/video Items, Playlists, playback sequencing, pause/seek/speed controls,
  per-Item media placement, embedded audio and background-music controls.
- Display-owned Project JSON v3 persistence and a fixed logical Canvas aspect.
- A Launcher and one admitted Player session per persistent display identity;
  repeated launches activate the existing role instead of creating a duplicate.
- Preview-only startup and reversible **Deploy / Undeploy**, preserving the Project,
  Canvas and Engine playback session.
- Shared graphics-device recovery, Explorer/shell-host invalidation handling and
  target-local recovery.
- A live, per-display Opaque/Transparent Desktop background preference:
  Transparent reveals the existing wallpaper in uncovered Desktop areas while
  preserving rendered media opacity. Preview remains opaque.
- English/Simplified Chinese interface selection and timeline thumbnails/cache.

FFmpeg decoding and audio processing are implemented, not merely planned.
Plugin/scripting APIs, alternate deployment modes and comprehensive mixed-display
soak qualification are not claimed. Existing desktop integration has been exercised
on a raised Windows 11 Explorer desktop; other shell layouts can be rejected.

## Runtime architecture

Each fixed Display Session owns one Engine and playback clock. Its Preview and
Desktop presentation targets share one D3D11 device/context and media upload path,
with independent native presentation resources and per-window client sizing.
The intended Desktop pacing target is selected while deployed; Undeploy removes
only that endpoint. Player coordinates through Engine public APIs; display identity
and Explorer hosting remain outside Rendering.

The authored Canvas is independent of physical display placement. Projects retain
media paths rather than bundling media files. Application preferences are separate
from Project JSON.

## Repository layout

```text
Engine/       Core runtime, domain/persistence, resources, rendering, Windows platform
Player/       Qt Widgets Launcher and fixed-display Player sessions
Sandbox/      Developer runtime exerciser, not the normal Player entry point
tests/        Automated tests, checked-in synthetic audio, optional native/soak tools
docs/         Architecture, persistence specifications and historical planning
notes/        Historical/design notes accepted for publication
vcpkg.json    nlohmann-json and FFmpeg manifest dependencies
CMakeLists.txt
```

There is no implemented standalone `Editor/` in this source tree. The historical
[roadmap](docs/05_ROADMAP.md) is a planning/checkpoint reference, not the current
feature list or an immediate-next-task mandate.

## Build prerequisites

- Windows x64, Visual Studio 2022 with the MSVC C++ toolchain and Windows SDK.
- C++20 support and CMake **3.25 or newer**.
- A compatible **Qt 6 MSVC x64** installation with Core, Widgets, Network and
  LinguistTools (`lrelease`), plus `windeployqt`.
- A bootstrapped **vcpkg** checkout, using the `x64-windows` triplet and manifest mode.
  The manifest installs `nlohmann-json` and `ffmpeg`; Qt is located separately.

The current FFmpeg deployment rules expect the FFmpeg 8 DLL ABI:
`avcodec-62`, `avformat-62`, `avutil-60`, `swscale-9`, `avfilter-11` and
`swresample-6`. Configure requires both Debug and Release dependency DLLs.
The manifest does not pin a vcpkg baseline: use a compatible port revision rather
than assuming any future dependency version will match these rules.

### Configure and build the Player

Run from the source root in PowerShell. Replace the placeholders with your local
Qt prefix, vcpkg checkout and chosen build directory; they are not literal paths.

```powershell
$qtRoot = "<QT_ROOT>"
$vcpkgRoot = "<VCPKG_ROOT>"
$buildDir = "<build-dir>"

cmake -S . -B $buildDir -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake" `
  "-DCMAKE_PREFIX_PATH=$qtRoot" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DBUILD_PLAYER=ON -DBUILD_TESTING=OFF -DBUILD_EDITOR=OFF

cmake --build $buildDir --config Release --target DesktopSceneEngine_QtPlayer
```

| Switch | Default | Current meaning |
| --- | --- | --- |
| `BUILD_PLAYER` | OFF | Build the Qt Player executable and its application resources/translations. |
| `BUILD_TESTING` | OFF | Build/register tests; the current Windows suite also needs `BUILD_PLAYER=ON`. |
| `BUILD_EDITOR` | OFF | Reserved option referencing an absent `Editor/`; keep OFF. |

The Player target is `DesktopSceneEngine_QtPlayer`. With the Visual Studio generator,
its executable is `<build-dir>/bin/Release/DesktopSceneEngine_QtPlayer.exe`
(or `bin/Debug/` for Debug). Post-build steps copy the FFmpeg DLLs, application
translations and available Qt Chinese catalog, and invoke `windeployqt`.

## Run and deploy

```powershell
& (Join-Path $buildDir "bin/Release/DesktopSceneEngine_QtPlayer.exe")
```

Launch without arguments to open the Launcher, select a persistent-capable display
and open its fixed Display Session. Add your own media through the Player UI.
Projects belong to that display identity; they are not automatically retargeted
to another display.

Start in Preview, then choose **Deploy** to attach the desktop presentation endpoint.
**Undeploy** returns to Preview-only without resetting playback. Closing the session
normally tears down its owned desktop endpoint. Transparent Desktop areas reveal
the existing Windows wallpaper; the preference is not authored Project state.

Native deployment needs the normal interactive Explorer desktop, a usable D3D11
device and a resolvable bound display. Unsupported shell topology, unavailable
display/device and recovery failures are not promises of silent fallback.
Administrator elevation, a service/session-zero host and arbitrary shell replacements
are not supported deployment workflows.

Sandbox is a developer tool. Its no-argument path expects local `TestResources/`
media, which is not published; compilation does not require that directory.

## Tests

After the Player configure above, enable tests in the same build directory:

```powershell
cmake -S . -B $buildDir -DBUILD_PLAYER=ON -DBUILD_TESTING=ON -DBUILD_EDITOR=OFF
cmake --build $buildDir --config Debug
ctest --test-dir $buildDir -C Debug --output-on-failure -j 1
```

Some tests create native windows, exercise real audio/graphics devices, or briefly
attach desktop content. Use a dedicated, unlocked interactive Windows desktop.
Foreground/modal cases require a foreground-qualified invocation; headless CI,
locked/secure desktops and arbitrary machines are not claimed to pass the full suite.

A clean clone has synthetic audio fixtures, but **does not include `TestResources/`**.
Nine image/video integration tests are not built or registered unless an external
fixture root is explicitly configured. This is visible at configure time, not a
runtime skip or a claim that omitted tests passed. Invalid/incomplete opt-in roots
fail configuration. The existing assertions still determine whether supplied media
is suitable.

See [test requirements and fixture policy](tests/README.md) for the exact matrix,
the `DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR` switch, registration regression,
and manual-tool limitations. The soak collector is not part of default CTest and
its historical canonical media identity is not shipped here.

## Documentation and contributing

Start with the [documentation index](docs/INDEX.md),
[architecture](docs/01_ARCHITECTURE.md),
[engineering principles](docs/EngineeringPrinciples.md), and
[project file I/O](docs/08_PROJECT_FILE_IO.md).
Some older planning/specification documents describe earlier checkpoints; this
README describes the prepared source's current capabilities. Keep changes scoped,
preserve ownership boundaries, and provide focused regression evidence.

## Releases

No prebuilt binary release is currently published.
Future Windows packages will be distributed through GitHub Releases.

## License and dependencies

DesktopSceneEngine is licensed under the [MIT License](LICENSE).

Third-party components retain their own licenses:
[Qt](https://www.qt.io/development/open-source-lgpl-obligations),
[FFmpeg](https://ffmpeg.org/legal.html), and
[nlohmann/json](https://json.nlohmann.me/home/license/).
Choosing the project's source license is separate from satisfying the obligations
for the particular third-party binaries in a future Windows package. Binary
redistribution qualification remains a separate Release gate.
