# DesktopSceneEngine

DesktopSceneEngine is a Windows desktop scene player and editor. Use the Launcher to open a Session for a display, edit a project's playlists, preview images and video, and deploy the scene to the Windows desktop. Projects can also include background music.

The Launcher, Session editor and background music window use React and TypeScript in WebView2. The native C++20 player uses Direct3D 11 and DirectComposition for presentation and FFmpeg for media processing. Qt 6 handles application lifecycle and native services such as file dialogs and the system tray.

## Download

Download `DesktopSceneEngine-win-x64.zip` from [GitHub Releases](https://github.com/tachibana-01/DesktopSceneEngine/releases). Extract the ZIP and run `DesktopSceneEngine_QtPlayer.exe` in the extracted `DesktopSceneEngine` folder. Keep the extracted files together.

The published package targets Windows 11 x64, the environment used for release testing. Other Windows versions have not been validated. Install the [Microsoft Visual C++ v14 x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/) and [Microsoft Edge WebView2 Evergreen Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/#download-section) if they are missing. The ZIP includes the required Qt and FFmpeg runtime libraries.

## Build on Windows

The release build used Windows 11 x64. To build from source, use:

- Visual Studio 2022 with **Desktop development with C++** and a Windows SDK. The release used SDK 10.0.26100.0. The project declares CMake 3.25 or newer.
- Qt 6.10.1 for MSVC x64, including Widgets, Network, Linguist Tools and `windeployqt`. This is the version used to build and package the release.
- Node.js 24.16.0 and npm 11.13.0, as specified in the [tooling lock](DesktopSceneEngineIII/dependency-lock.json). The restore script fetches and verifies the pinned WebView2 SDK 1.0.4258.31. The separate Evergreen Runtime is needed to run the built app.
- A bootstrapped vcpkg checkout for FFmpeg and nlohmann/json. Use `x64-windows` and a revision that provides the FFmpeg 8 DLL ABI in both Debug and Release. The [vcpkg revision used for the release](https://github.com/microsoft/vcpkg/commit/c1d80d9cb071c3f4a98c67c1196b137cc5b72918) is a known starting point because the manifest does not pin a baseline.

Run these commands from the repository root in PowerShell. Set `VCPKG_ROOT` to your vcpkg checkout and `CMAKE_PREFIX_PATH` to the Qt MSVC x64 installation prefix, for example the `6.10.1\msvc2022_64` directory.

```powershell
powershell -NoProfile -File DesktopSceneEngineIII/scripts/Restore-WebView2.ps1
npm.cmd --prefix DesktopSceneEngineIII/Frontend ci
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  "-DCMAKE_PREFIX_PATH=$env:CMAKE_PREFIX_PATH" `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DBUILD_PLAYER=ON
cmake --build build --config Release --target DesktopSceneEngine_QtPlayer
```

### Run

Start `build/bin/Release/DesktopSceneEngine_QtPlayer.exe` without arguments to open the Launcher. Select a display and choose **Get started** to create a Session, or **Open workspace** to return to the display's existing Session. The build stages the frontend package and native dependencies next to the executable.

## Tests

Enable `BUILD_TESTING=ON` and `BUILD_PLAYER=ON` in a separate build directory and run CTest. Default tests use generated media and small checked-in audio fixtures. See the [test instructions](tests/README.md) for optional external fixtures and native test requirements.

```powershell
npm.cmd --prefix DesktopSceneEngineIII/Frontend run typecheck
```

Some frontend DOM checks require Chrome, which can be located with `CHROME_PATH`. Use a dedicated browser and Vite server for DevTools checks that use ports 9227 and 5173. Some of these checks close or navigate tabs.

## Source layout

- `Engine/` implements media, rendering, audio, scene and persistence.
- `Player/` implements the Windows application, Sessions, WebView2 hosts and platform integration.
- `DesktopSceneEngineIII/Frontend/` contains the React application and frontend tests.
- `Sandbox/` is a separate development executable.
- `tests/` contains native test targets and fixtures.

See the [project JSON specification](docs/06_AUTHORED_PROJECT_JSON_V2.md) for the authored file format.

## Distribution

Use the [Windows staging script](tools/Stage-WindowsRelease.ps1) to prepare a new distribution directory from the Release output before creating the ZIP. It requires PowerShell 7, `dumpbin`, the matching Qt source archives and the corresponding vcpkg build inputs. The [distribution notices](tools/windows-release/THIRD_PARTY_NOTICES.md) describe the prerequisites and third-party licenses, sources and build materials included in the package.

## License

DesktopSceneEngine source is distributed under the [MIT License](LICENSE). Third-party components retain their own licenses.
