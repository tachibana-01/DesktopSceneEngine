# DesktopSceneEngine

DesktopSceneEngine is a Windows desktop scene player and editor. Launcher opens a Session for a display. A Session edits a project, previews its media and playlists, and can deploy the preview to the Windows desktop. Projects can also contain background-music queues.

The Windows Player uses C++20, Direct3D 11, DirectComposition and FFmpeg. The Launcher, Session editor and background-music window use React and TypeScript in WebView2. Qt 6 provides application and Windows platform services. Published Windows x64 builds and their distribution materials are on the [release page](https://github.com/tachibana-01/DesktopSceneEngine/releases).

## Build on Windows

Use Windows 11 x64, Visual Studio 2022 with the Desktop development with C++ workload, a Windows SDK, CMake 3.25 or newer, and Qt 6.10.1 for MSVC x64. Install the Microsoft Edge WebView2 Evergreen Runtime. Use vcpkg for FFmpeg and nlohmann/json. The repository's tooling lock records the expected Node.js, npm and WebView2 SDK versions.

Run the following from the repository root in PowerShell. Set `VCPKG_ROOT` to your vcpkg checkout and `CMAKE_PREFIX_PATH` to your Qt MSVC x64 installation.

```powershell
powershell -NoProfile -File DesktopSceneEngineIII/scripts/Restore-WebView2.ps1
npm.cmd --prefix DesktopSceneEngineIII/Frontend ci
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_PLAYER=ON "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" "-DCMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH"
cmake --build build --config Release --target DesktopSceneEngine_QtPlayer
```

Open `build/bin/Release/DesktopSceneEngine_QtPlayer.exe`. The build places the native dependencies and the frontend package next to the executable. Launcher starts the Session from the same executable and package.

For redistribution, use the [Windows staging script](tools/Stage-WindowsRelease.ps1) with the Release output, the matching Qt source archives and the vcpkg build inputs. It requires PowerShell 7 and `dumpbin`. The [distribution notices](tools/windows-release/THIRD_PARTY_NOTICES.md) cover the Windows prerequisites and the included source materials.

## Tests

Enable `BUILD_TESTING` in a separate build tree and use CTest. The default tests use generated media and small checked-in synthetic audio. Selected native media tests can use a separate, explicitly configured fixture directory. See the [test instructions](tests/README.md) for fixture requirements and what CTest reports when media is absent.

```powershell
npm.cmd --prefix DesktopSceneEngineIII/Frontend run typecheck
```

Some frontend DOM checks require Chrome. Tests that launch Chrome locate it through the Windows installation directories or the `CHROME_PATH` environment variable. Run the legacy DevTools checks only with a dedicated test browser and Vite server. They use ports 9227 and 5173, respectively, and some close or navigate tabs in the dedicated browser.

## Source layout

- `Engine/` contains the media, rendering, audio, scene and persistence implementation.
- `Player/` contains the Windows application, Session, WebView2 hosts and native platform integration.
- `DesktopSceneEngineIII/Frontend/` contains the React application and frontend tests.
- `Sandbox/` is a separate development executable. It is not installed with the Player.
- `tests/` contains native test targets and test fixtures.

The authored project format is described in the [project JSON specification](docs/06_AUTHORED_PROJECT_JSON_V2.md).

## License

DesktopSceneEngine source is distributed under the [MIT License](LICENSE). Third-party components retain their own licenses. The Windows distribution includes its third-party notices and source information.
