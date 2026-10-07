add_library(
    DesktopSceneEngine
    STATIC
)


add_library(
    DesktopSceneEngine::Engine
    ALIAS
    DesktopSceneEngine
)


find_package(
    nlohmann_json
    CONFIG
    REQUIRED
)


find_package(
    FFMPEG
    REQUIRED
)


target_sources(
    DesktopSceneEngine
    PRIVATE

    src/DisplayIdentity.cpp
    src/Audio/AudioMixer.cpp
    src/Audio/AudioSampleConversion.cpp
    src/Audio/EmbeddedAudioMixerInput.cpp
    src/Audio/MixedOutputBridge.cpp
    src/Core/Engine.cpp
    src/Music/BackgroundMusicExecutor.cpp
    src/Music/BackgroundMusicOrchestrator.cpp
    src/Music/BackgroundMusicRuntime.cpp
    src/Music/MusicSession.cpp
    src/Music/SequentialAudioMixerInput.cpp
    src/Infrastructure/Json/AuthoredProjectJson.cpp
    src/Infrastructure/Json/AuthoredProjectValidation.cpp
    src/Infrastructure/Json/ProjectJson.cpp
    src/Infrastructure/Persistence/ProjectFile.cpp
    src/Platform/Audio/AudioOutput.cpp
    src/Platform/Audio/DefaultAudioEndpointWatcher.cpp
    src/Platform/Windows/DesktopWindowHost.cpp
    src/Platform/Windows/DesktopPresentationHost.cpp
    src/Platform/Windows/NativeWindowClientSize.cpp
    src/Platform/Windows/WindowsDisplayIdentifier.cpp
    src/Platform/Windows/WindowsDisplayEnumerator.cpp
    src/Resources/AudioResource.cpp
    src/Resources/ImageResource.cpp
    src/Resources/PreviewVideoDecoder.cpp
    src/Resources/RepresentativeFrameCache.cpp
    src/Resources/RepresentativeFrameMemoryCache.cpp
    src/Resources/RepresentativeFramePng.cpp
    src/Resources/RepresentativeFrameSampling.cpp
    src/Resources/RepresentativeFrameStore.cpp
    src/Resources/SequentialAudioSource.cpp
    src/Resources/VideoResource.cpp
    src/Rendering/GraphicsBackend.cpp
    src/Rendering/Renderer.cpp
    src/Runtime/Playback/PlaybackRuntime.cpp
    src/Runtime/Scene/SceneRuntime.cpp
)


target_include_directories(
    DesktopSceneEngine
    PUBLIC

    ${CMAKE_CURRENT_SOURCE_DIR}/include
)


target_compile_features(
    DesktopSceneEngine
    PUBLIC

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:GNU>:-Wall>
    $<$<CXX_COMPILER_ID:GNU>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU>:-Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall>
    $<$<CXX_COMPILER_ID:Clang>:-Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Wpedantic>
)


target_link_libraries(
    DesktopSceneEngine
    PRIVATE

    nlohmann_json::nlohmann_json
    ${FFMPEG_libavformat_LIBRARY}
    ${FFMPEG_libavcodec_LIBRARY}
    ${FFMPEG_libavfilter_LIBRARY}
    ${FFMPEG_libavutil_LIBRARY}
    ${FFMPEG_libswscale_LIBRARY}
    ${FFMPEG_libswresample_LIBRARY}
)


target_include_directories(
    DesktopSceneEngine
    PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFMPEG_INCLUDE_DIRS}
)


if(WIN32)

    target_link_libraries(
        DesktopSceneEngine
        PRIVATE

        user32
        d3d11
        dxgi
        dcomp
        d3dcompiler
        comctl32
        windowscodecs
        ole32
        uuid
    )

endif()


set_target_properties(
    DesktopSceneEngine
    PROPERTIES

    OUTPUT_NAME "DesktopSceneEngine"
)
