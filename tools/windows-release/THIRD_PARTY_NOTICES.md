# Third-party notices

## Windows requirements

DesktopSceneEngine is built for Windows 11 x64. Install the [Microsoft Visual C++ v14 x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/) and the [Microsoft Edge WebView2 Evergreen Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) if they are missing. Microsoft distributes those components separately. Windows supplies Direct3D, DirectComposition, D3DCompiler, ICU, Schannel and the Universal CRT.

## Qt

DesktopSceneEngine uses Qt 6.10.1. The adjacent Qt Core, GUI, Network, Widgets and SVG DLLs and their Qt plugins are dynamically linked or loaded under the GNU Lesser General Public License version 3. This package includes the LGPL version 3 and GPL version 3 texts, the actual embedded component attributions and the complete matching Qt Base and Qt SVG sources. Qt source archives are available in `third_party/sources/`. Build configuration is in `third_party/build-info/`.

The separate Qt `i18n/qtbase_zh_CN.qm` catalog is distributed under BSD-3-Clause with its copyright and terms in `third_party/licenses/Qt-Translations-BSD-3-Clause.txt`. Its source catalog is part of [Qt Translations 6.10.1](https://download.qt.io/official_releases/qt/6.10/6.10.1/submodules/qttranslations-everywhere-src-6.10.1.tar.xz.mirrorlist).

Qt's Windows entry point is statically linked under BSD-3-Clause. Its copyright and terms are in `third_party/licenses/Qt-EntryPoint-BSD-3-Clause.txt`. Its source is included in the Qt Base archive.

This software is based in part on the work of the Independent JPEG Group. Portions of this software are copyright © The FreeType Project. All rights reserved. FreeType is included under the FreeType Project License.

Qt Network incorporates data from the Public Suffix List under the Mozilla Public License 2.0. Its compiled data and build scripts are in the Qt Base source archive. The original data is available at the [corresponding Public Suffix List revision](https://github.com/publicsuffix/list/blob/2141aa8a1f634c2b839175c559ceaac6a849592c/public_suffix_list.dat).

## FFmpeg

This software uses code from [FFmpeg](https://ffmpeg.org/) licensed under LGPL version 2.1 or later. The six adjacent FFmpeg DLLs are dynamically linked. The matching FFmpeg source archive includes the upstream source, the applied vcpkg patches, the port recipe and the Release build options. It is available in `third_party/sources/`.

FFmpeg includes work of the Independent JPEG Group. The vcpkg patch set makes no changes to FFmpeg's `jfdctfst.c`, `jfdctint_template.c`, or `jrevdct.c`.

## Replacing the shared libraries

You may replace the included Qt and FFmpeg shared libraries and Qt plugins with compatible modified builds. Close the app and replace the matching files in the extracted application folder, preserving the original filenames and plugin subdirectories. Use Windows x64 DLLs with a compatible exported interface. Run `DesktopSceneEngine_QtPlayer.exe` from the same folder. DesktopSceneEngine permits reverse engineering to debug modifications to these LGPL libraries.

## WebView2 Loader and other components

The Microsoft WebView2 SDK Loader is statically linked under BSD-3-Clause. The SDK copyright, conditions, disclaimer and supplied notice are in `third_party/licenses/WebView2-LICENSE.txt` and `WebView2-NOTICE.txt`.

The app includes compiled nlohmann/json header code, frontend JavaScript from React, React DOM and Scheduler, and the Vite module-preload and Rolldown CommonJS helpers. Their MIT notices are in `third_party/licenses/`. The exact component versions are in `third_party/build-info/component-versions.txt`.
