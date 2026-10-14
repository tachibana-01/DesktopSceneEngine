include("${CMAKE_CURRENT_LIST_DIR}/cmake/ExternalFixtures.cmake")

add_test(NAME DesktopSceneEngine_PublicFixtureRegistrationTest
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_LIST_DIR}/cmake/PublicFixtureRegistrationTest.cmake")


find_package(
    nlohmann_json
    CONFIG
    REQUIRED
)


find_package(
    Qt6
    CONFIG
    REQUIRED
    COMPONENTS Core Widgets
)


add_executable(
    DesktopSceneEngine_WorkspaceModelTest
)


target_sources(
    DesktopSceneEngine_WorkspaceModelTest
    PRIVATE

    Player/WorkspaceModelTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


target_include_directories(
    DesktopSceneEngine_WorkspaceModelTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/include
)


target_compile_features(
    DesktopSceneEngine_WorkspaceModelTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_WorkspaceModelTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_WorkspaceModelTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Core
)


add_test(
    NAME

    DesktopSceneEngine_WorkspaceModelTest

    COMMAND

    DesktopSceneEngine_WorkspaceModelTest
)


add_executable(
    DesktopSceneEngine_ProjectDocumentStateTest
)


target_sources(
    DesktopSceneEngine_ProjectDocumentStateTest
    PRIVATE

    Player/ProjectDocumentStateTest.cpp
)


target_include_directories(
    DesktopSceneEngine_ProjectDocumentStateTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_ProjectDocumentStateTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_ProjectDocumentStateTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_test(
    NAME

    DesktopSceneEngine_ProjectDocumentStateTest

    COMMAND

    DesktopSceneEngine_ProjectDocumentStateTest
)


add_executable(
    DesktopSceneEngine_BackgroundMusicControlAdapterTest
)


target_sources(
    DesktopSceneEngine_BackgroundMusicControlAdapterTest
    PRIVATE

    Player/BackgroundMusicControlAdapterTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
    ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


target_include_directories(
    DesktopSceneEngine_BackgroundMusicControlAdapterTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/include
)


target_compile_features(
    DesktopSceneEngine_BackgroundMusicControlAdapterTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_BackgroundMusicControlAdapterTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_BackgroundMusicControlAdapterTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Core
)


add_test(
    NAME

    DesktopSceneEngine_BackgroundMusicControlAdapterTest

    COMMAND

    DesktopSceneEngine_BackgroundMusicControlAdapterTest
)


add_executable(
    DesktopSceneEngine_ActivePlaybackProjectionTest
)


target_sources(
    DesktopSceneEngine_ActivePlaybackProjectionTest
    PRIVATE

    Player/ActivePlaybackProjectionTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


target_include_directories(
    DesktopSceneEngine_ActivePlaybackProjectionTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_ActivePlaybackProjectionTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_ActivePlaybackProjectionTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_ActivePlaybackProjectionTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Core
)


add_test(
    NAME

    DesktopSceneEngine_ActivePlaybackProjectionTest

    COMMAND

    DesktopSceneEngine_ActivePlaybackProjectionTest
)


if(DSE_EXTERNAL_FIXTURES_AVAILABLE)

add_executable(
    DesktopSceneEngine_ActiveProjectionRebuildTest
)


target_sources(
    DesktopSceneEngine_ActiveProjectionRebuildTest
    PRIVATE

    Player/ActiveProjectionRebuildTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


target_include_directories(
    DesktopSceneEngine_ActiveProjectionRebuildTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_ActiveProjectionRebuildTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_ActiveProjectionRebuildTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_ActiveProjectionRebuildTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR="${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
)


target_link_libraries(
    DesktopSceneEngine_ActiveProjectionRebuildTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Core
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_ActiveProjectionRebuildTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_ActiveProjectionRebuildTest

    COMMAND

    DesktopSceneEngine_ActiveProjectionRebuildTest
)

endif()


add_executable(
    DesktopSceneEngine_PreviewViewportHostTest
)


target_sources(
    DesktopSceneEngine_PreviewViewportHostTest
    PRIVATE

    Player/PreviewViewportHostTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
    ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
)


target_include_directories(
    DesktopSceneEngine_PreviewViewportHostTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/include
)


target_compile_features(
    DesktopSceneEngine_PreviewViewportHostTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_PreviewViewportHostTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_PreviewViewportHostTest
    PRIVATE

    Qt6::Widgets
)


add_test(
    NAME

    DesktopSceneEngine_PreviewViewportHostTest

    COMMAND

    DesktopSceneEngine_PreviewViewportHostTest
)


add_executable(
    DesktopSceneEngine_TimelineUtilsTest
)


target_sources(
    DesktopSceneEngine_TimelineUtilsTest
    PRIVATE

    Player/TimelineUtilsTest.cpp
)


target_include_directories(
    DesktopSceneEngine_TimelineUtilsTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_TimelineUtilsTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_TimelineUtilsTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_test(
    NAME

    DesktopSceneEngine_TimelineUtilsTest

    COMMAND

    DesktopSceneEngine_TimelineUtilsTest
)


add_executable(
    DesktopSceneEngine_EffectiveVolumeTest
)


target_sources(
    DesktopSceneEngine_EffectiveVolumeTest
    PRIVATE

    Player/EffectiveVolumeTest.cpp
)


target_include_directories(
    DesktopSceneEngine_EffectiveVolumeTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_EffectiveVolumeTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_EffectiveVolumeTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_test(
    NAME

    DesktopSceneEngine_EffectiveVolumeTest

    COMMAND

    DesktopSceneEngine_EffectiveVolumeTest
)


add_executable(
    DesktopSceneEngine_EngineLifecycleTest
)


target_sources(
    DesktopSceneEngine_EngineLifecycleTest
    PRIVATE

    Core/EngineLifecycleTest.cpp
)


target_compile_features(
    DesktopSceneEngine_EngineLifecycleTest
    PRIVATE

    cxx_std_20
)


target_link_libraries(
    DesktopSceneEngine_EngineLifecycleTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_EngineLifecycleTest

    COMMAND

    DesktopSceneEngine_EngineLifecycleTest
)


add_executable(
    DesktopSceneEngine_BackgroundMusicEngineIntegrationTest
)


target_sources(
    DesktopSceneEngine_BackgroundMusicEngineIntegrationTest
    PRIVATE

    Core/BackgroundMusicEngineIntegrationTest.cpp
)


target_compile_features(
    DesktopSceneEngine_BackgroundMusicEngineIntegrationTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_BackgroundMusicEngineIntegrationTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_include_directories(
    DesktopSceneEngine_BackgroundMusicEngineIntegrationTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_definitions(
    DesktopSceneEngine_BackgroundMusicEngineIntegrationTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/TestData/Audio"
)


target_link_libraries(
    DesktopSceneEngine_BackgroundMusicEngineIntegrationTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_BackgroundMusicEngineIntegrationTest

    COMMAND

    DesktopSceneEngine_BackgroundMusicEngineIntegrationTest
)


add_executable(
    DesktopSceneEngine_BackgroundMusicFinalIntegrationTest
)


target_sources(
    DesktopSceneEngine_BackgroundMusicFinalIntegrationTest
    PRIVATE

    Core/BackgroundMusicFinalIntegrationTest.cpp
)


target_compile_features(
    DesktopSceneEngine_BackgroundMusicFinalIntegrationTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_BackgroundMusicFinalIntegrationTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_include_directories(
    DesktopSceneEngine_BackgroundMusicFinalIntegrationTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_definitions(
    DesktopSceneEngine_BackgroundMusicFinalIntegrationTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/TestData/Audio"
)


target_link_libraries(
    DesktopSceneEngine_BackgroundMusicFinalIntegrationTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_BackgroundMusicFinalIntegrationTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_BackgroundMusicFinalIntegrationTest

    COMMAND

    DesktopSceneEngine_BackgroundMusicFinalIntegrationTest
)


add_executable(
    DesktopSceneEngine_AggregateAudioOutputPolicyTest
)


target_sources(
    DesktopSceneEngine_AggregateAudioOutputPolicyTest
    PRIVATE

    Core/AggregateAudioOutputPolicyTest.cpp
)


target_compile_features(
    DesktopSceneEngine_AggregateAudioOutputPolicyTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_AggregateAudioOutputPolicyTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_include_directories(
    DesktopSceneEngine_AggregateAudioOutputPolicyTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


add_test(
    NAME

    DesktopSceneEngine_AggregateAudioOutputPolicyTest

    COMMAND

    DesktopSceneEngine_AggregateAudioOutputPolicyTest
)


add_executable(
    DesktopSceneEngine_SharedAudioOutputRecoveryPolicyTest
)


target_sources(
    DesktopSceneEngine_SharedAudioOutputRecoveryPolicyTest
    PRIVATE

    Core/SharedAudioOutputRecoveryPolicyTest.cpp
)


target_compile_features(
    DesktopSceneEngine_SharedAudioOutputRecoveryPolicyTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_SharedAudioOutputRecoveryPolicyTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_include_directories(
    DesktopSceneEngine_SharedAudioOutputRecoveryPolicyTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


add_test(
    NAME

    DesktopSceneEngine_SharedAudioOutputRecoveryPolicyTest

    COMMAND

    DesktopSceneEngine_SharedAudioOutputRecoveryPolicyTest
)


add_executable(
    DesktopSceneEngine_AudioOutputRecoveryEngineTest
)


target_sources(
    DesktopSceneEngine_AudioOutputRecoveryEngineTest
    PRIVATE

    Core/AudioOutputRecoveryEngineTest.cpp
)


target_compile_features(
    DesktopSceneEngine_AudioOutputRecoveryEngineTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_AudioOutputRecoveryEngineTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_include_directories(
    DesktopSceneEngine_AudioOutputRecoveryEngineTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_definitions(
    DesktopSceneEngine_AudioOutputRecoveryEngineTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/TestData/Audio"
)


target_link_libraries(
    DesktopSceneEngine_AudioOutputRecoveryEngineTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_AudioOutputRecoveryEngineTest

    COMMAND

    DesktopSceneEngine_AudioOutputRecoveryEngineTest
)


add_executable(
    DesktopSceneEngine_DefaultAudioEndpointWatcherTest
)


target_sources(
    DesktopSceneEngine_DefaultAudioEndpointWatcherTest
    PRIVATE

    Platform/DefaultAudioEndpointWatcherTest.cpp
)


target_compile_features(
    DesktopSceneEngine_DefaultAudioEndpointWatcherTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_DefaultAudioEndpointWatcherTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_include_directories(
    DesktopSceneEngine_DefaultAudioEndpointWatcherTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_link_libraries(
    DesktopSceneEngine_DefaultAudioEndpointWatcherTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_DefaultAudioEndpointWatcherTest

    COMMAND

    DesktopSceneEngine_DefaultAudioEndpointWatcherTest
)


add_executable(
    DesktopSceneEngine_ProjectJsonTest
)


target_sources(
    DesktopSceneEngine_ProjectJsonTest
    PRIVATE

    Infrastructure/ProjectJsonTest.cpp
)


target_include_directories(
    DesktopSceneEngine_ProjectJsonTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_ProjectJsonTest
    PRIVATE

    cxx_std_20
)


target_link_libraries(
    DesktopSceneEngine_ProjectJsonTest
    PRIVATE

    DesktopSceneEngine::Engine
    nlohmann_json::nlohmann_json
)


add_test(
    NAME

    DesktopSceneEngine_ProjectJsonTest

    COMMAND

    DesktopSceneEngine_ProjectJsonTest
)


add_executable(
    DesktopSceneEngine_AuthoredProjectJsonTest
)


target_sources(
    DesktopSceneEngine_AuthoredProjectJsonTest
    PRIVATE

    Infrastructure/AuthoredProjectJsonTest.cpp
)


target_include_directories(
    DesktopSceneEngine_AuthoredProjectJsonTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_AuthoredProjectJsonTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_AuthoredProjectJsonTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_AuthoredProjectJsonTest
    PRIVATE

    DesktopSceneEngine::Engine
    nlohmann_json::nlohmann_json
)


add_test(
    NAME

    DesktopSceneEngine_AuthoredProjectJsonTest

    COMMAND

    DesktopSceneEngine_AuthoredProjectJsonTest
)


add_executable(
    DesktopSceneEngine_ProjectFileTest
)


add_executable(
    DesktopSceneEngine_SceneRuntimeTest
)


target_sources(
    DesktopSceneEngine_SceneRuntimeTest
    PRIVATE

    Runtime/SceneRuntimeTest.cpp
)


target_include_directories(
    DesktopSceneEngine_SceneRuntimeTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src/Runtime/Scene
)


target_compile_features(
    DesktopSceneEngine_SceneRuntimeTest
    PRIVATE

    cxx_std_20
)


target_link_libraries(
    DesktopSceneEngine_SceneRuntimeTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_SceneRuntimeTest

    COMMAND

    DesktopSceneEngine_SceneRuntimeTest
)


add_executable(
    DesktopSceneEngine_PlaybackRuntimeTest
)


target_sources(
    DesktopSceneEngine_PlaybackRuntimeTest
    PRIVATE

    Runtime/PlaybackRuntimeTest.cpp
)


target_include_directories(
    DesktopSceneEngine_PlaybackRuntimeTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src/Runtime/Playback
)


target_compile_features(
    DesktopSceneEngine_PlaybackRuntimeTest
    PRIVATE

    cxx_std_20
)


target_link_libraries(
    DesktopSceneEngine_PlaybackRuntimeTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_PlaybackRuntimeTest

    COMMAND

    DesktopSceneEngine_PlaybackRuntimeTest
)


target_sources(
    DesktopSceneEngine_ProjectFileTest
    PRIVATE

    Infrastructure/ProjectFileTest.cpp
)


target_include_directories(
    DesktopSceneEngine_ProjectFileTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_ProjectFileTest
    PRIVATE

    cxx_std_20
)


target_link_libraries(
    DesktopSceneEngine_ProjectFileTest
    PRIVATE

    DesktopSceneEngine::Engine
    nlohmann_json::nlohmann_json
)


add_test(
    NAME

    DesktopSceneEngine_ProjectFileTest

    COMMAND

    DesktopSceneEngine_ProjectFileTest
)


add_executable(
    DesktopSceneEngine_AuthoredProjectFileTest
)


target_sources(
    DesktopSceneEngine_AuthoredProjectFileTest
    PRIVATE

    Infrastructure/AuthoredProjectFileTest.cpp
)


target_include_directories(
    DesktopSceneEngine_AuthoredProjectFileTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_AuthoredProjectFileTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_AuthoredProjectFileTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_AuthoredProjectFileTest
    PRIVATE

    DesktopSceneEngine::Engine
    nlohmann_json::nlohmann_json
)


add_test(
    NAME

    DesktopSceneEngine_AuthoredProjectFileTest

    COMMAND

    DesktopSceneEngine_AuthoredProjectFileTest
)


add_executable(
    DesktopSceneEngine_ProjectBindingTest
)


target_sources(
    DesktopSceneEngine_ProjectBindingTest
    PRIVATE

    Core/ProjectBindingTest.cpp
)


target_compile_features(
    DesktopSceneEngine_ProjectBindingTest
    PRIVATE

    cxx_std_20
)


target_link_libraries(
    DesktopSceneEngine_ProjectBindingTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_ProjectBindingTest

    COMMAND

    DesktopSceneEngine_ProjectBindingTest
)


if(DSE_EXTERNAL_FIXTURES_AVAILABLE)

add_executable(
    DesktopSceneEngine_EngineSceneNavigationTest
)


target_sources(
    DesktopSceneEngine_EngineSceneNavigationTest
    PRIVATE

    Core/EngineSceneNavigationTest.cpp
)


target_compile_features(
    DesktopSceneEngine_EngineSceneNavigationTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_EngineSceneNavigationTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_EngineSceneNavigationTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR="${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
)


target_link_libraries(
    DesktopSceneEngine_EngineSceneNavigationTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_EngineSceneNavigationTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_EngineSceneNavigationTest

    COMMAND

    DesktopSceneEngine_EngineSceneNavigationTest
)

endif()


if(DSE_EXTERNAL_FIXTURES_AVAILABLE)

add_executable(
    DesktopSceneEngine_EngineSeekTest
)


target_sources(
    DesktopSceneEngine_EngineSeekTest
    PRIVATE

    Core/EngineSeekTest.cpp
)


target_compile_features(
    DesktopSceneEngine_EngineSeekTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_EngineSeekTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_include_directories(
    DesktopSceneEngine_EngineSeekTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_definitions(
    DesktopSceneEngine_EngineSeekTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR="${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
    DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/TestData/Audio"
)


target_link_libraries(
    DesktopSceneEngine_EngineSeekTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_EngineSeekTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_EngineSeekTest

    COMMAND

    DesktopSceneEngine_EngineSeekTest
)

endif()


add_executable(
    DesktopSceneEngine_EmbeddedAudioStartFrameTest
)


target_sources(
    DesktopSceneEngine_EmbeddedAudioStartFrameTest
    PRIVATE

    Core/EmbeddedAudioStartFrameTest.cpp
)


target_include_directories(
    DesktopSceneEngine_EmbeddedAudioStartFrameTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_EmbeddedAudioStartFrameTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_EmbeddedAudioStartFrameTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_test(
    NAME

    DesktopSceneEngine_EmbeddedAudioStartFrameTest

    COMMAND

    DesktopSceneEngine_EmbeddedAudioStartFrameTest
)


add_executable(
    DesktopSceneEngine_EmbeddedAudioLifecycleTest
)


target_sources(
    DesktopSceneEngine_EmbeddedAudioLifecycleTest
    PRIVATE

    Core/EmbeddedAudioLifecycleTest.cpp
)


target_include_directories(
    DesktopSceneEngine_EmbeddedAudioLifecycleTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_EmbeddedAudioLifecycleTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_EmbeddedAudioLifecycleTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_test(
    NAME

    DesktopSceneEngine_EmbeddedAudioLifecycleTest

    COMMAND

    DesktopSceneEngine_EmbeddedAudioLifecycleTest
)


add_executable(
    DesktopSceneEngine_VideoResourceTest
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_VideoResourceTest)

endif()


target_sources(
    DesktopSceneEngine_VideoResourceTest
    PRIVATE

    Resources/VideoResourceTest.cpp
)


target_include_directories(
    DesktopSceneEngine_VideoResourceTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_VideoResourceTest
    PRIVATE

    cxx_std_20
)


target_link_libraries(
    DesktopSceneEngine_VideoResourceTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_VideoResourceTest

    COMMAND

    DesktopSceneEngine_VideoResourceTest
)


add_executable(
    DesktopSceneEngine_AudioResourceTest
)


target_sources(
    DesktopSceneEngine_AudioResourceTest
    PRIVATE

    Resources/AudioResourceTest.cpp
)


target_include_directories(
    DesktopSceneEngine_AudioResourceTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_AudioResourceTest
    PRIVATE

    cxx_std_20
)


target_link_libraries(
    DesktopSceneEngine_AudioResourceTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_AudioResourceTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_AudioResourceTest

    COMMAND

    DesktopSceneEngine_AudioResourceTest
    "${CMAKE_SOURCE_DIR}/tests/TestData/Audio/SequentialAudioSourceCompressed.mp3"
)


add_executable(
    DesktopSceneEngine_SequentialAudioSourceTest
)


target_sources(
    DesktopSceneEngine_SequentialAudioSourceTest
    PRIVATE

    Resources/SequentialAudioSourceTest.cpp
)


target_include_directories(
    DesktopSceneEngine_SequentialAudioSourceTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_SequentialAudioSourceTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_SequentialAudioSourceTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_SequentialAudioSourceTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/TestData/Audio"
)


target_link_libraries(
    DesktopSceneEngine_SequentialAudioSourceTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_SequentialAudioSourceTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_SequentialAudioSourceTest

    COMMAND

    DesktopSceneEngine_SequentialAudioSourceTest
)


add_executable(
    DesktopSceneEngine_SequentialAudioMixerInputTest
)


target_sources(
    DesktopSceneEngine_SequentialAudioMixerInputTest
    PRIVATE

    Music/SequentialAudioMixerInputTest.cpp
)


target_include_directories(
    DesktopSceneEngine_SequentialAudioMixerInputTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_SequentialAudioMixerInputTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_SequentialAudioMixerInputTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_SequentialAudioMixerInputTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/TestData/Audio"
)


target_link_libraries(
    DesktopSceneEngine_SequentialAudioMixerInputTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_SequentialAudioMixerInputTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_SequentialAudioMixerInputTest

    COMMAND

    DesktopSceneEngine_SequentialAudioMixerInputTest
)


add_executable(
    DesktopSceneEngine_BackgroundMusicExecutorTest
)


target_sources(
    DesktopSceneEngine_BackgroundMusicExecutorTest
    PRIVATE

    Music/BackgroundMusicExecutorTest.cpp
)


target_include_directories(
    DesktopSceneEngine_BackgroundMusicExecutorTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_BackgroundMusicExecutorTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_BackgroundMusicExecutorTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_BackgroundMusicExecutorTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_SEQUENTIAL_AUDIO_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/TestData/Audio"
)


target_link_libraries(
    DesktopSceneEngine_BackgroundMusicExecutorTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_BackgroundMusicExecutorTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_BackgroundMusicExecutorTest

    COMMAND

    DesktopSceneEngine_BackgroundMusicExecutorTest
)


add_executable(
    DesktopSceneEngine_AudioSampleConversionTest
)


target_sources(
    DesktopSceneEngine_AudioSampleConversionTest
    PRIVATE

    Audio/AudioSampleConversionTest.cpp
)


target_include_directories(
    DesktopSceneEngine_AudioSampleConversionTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_AudioSampleConversionTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_AudioSampleConversionTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_AudioSampleConversionTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_AudioSampleConversionTest

    COMMAND

    DesktopSceneEngine_AudioSampleConversionTest
)


add_executable(
    DesktopSceneEngine_EmbeddedAudioMixerInputTest
)


target_sources(
    DesktopSceneEngine_EmbeddedAudioMixerInputTest
    PRIVATE

    Audio/EmbeddedAudioMixerInputTest.cpp
)


target_include_directories(
    DesktopSceneEngine_EmbeddedAudioMixerInputTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_EmbeddedAudioMixerInputTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_EmbeddedAudioMixerInputTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_EmbeddedAudioMixerInputTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_EmbeddedAudioMixerInputTest

    COMMAND

    DesktopSceneEngine_EmbeddedAudioMixerInputTest
)


add_executable(
    DesktopSceneEngine_MixedOutputBridgeTest
)


target_sources(
    DesktopSceneEngine_MixedOutputBridgeTest
    PRIVATE

    Audio/MixedOutputBridgeTest.cpp
)


target_include_directories(
    DesktopSceneEngine_MixedOutputBridgeTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_MixedOutputBridgeTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_MixedOutputBridgeTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_MixedOutputBridgeTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_MixedOutputBridgeTest

    COMMAND

    DesktopSceneEngine_MixedOutputBridgeTest
)


add_executable(
    DesktopSceneEngine_AudioMixerTest
)


target_sources(
    DesktopSceneEngine_AudioMixerTest
    PRIVATE

    Audio/AudioMixerTest.cpp
)


target_include_directories(
    DesktopSceneEngine_AudioMixerTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_AudioMixerTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_AudioMixerTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_AudioMixerTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_AudioMixerTest

    COMMAND

    DesktopSceneEngine_AudioMixerTest
)


add_executable(
    DesktopSceneEngine_AudioOutputTest
)


target_sources(
    DesktopSceneEngine_AudioOutputTest
    PRIVATE

    Platform/AudioOutputTest.cpp
)


target_include_directories(
    DesktopSceneEngine_AudioOutputTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_AudioOutputTest
    PRIVATE

    cxx_std_20
)


target_link_libraries(
    DesktopSceneEngine_AudioOutputTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_AudioOutputTest

    COMMAND

    DesktopSceneEngine_AudioOutputTest
)


if(DSE_EXTERNAL_FIXTURES_AVAILABLE)

add_executable(
    DesktopSceneEngine_PreviewVideoDecoderTest
)


target_sources(
    DesktopSceneEngine_PreviewVideoDecoderTest
    PRIVATE

    Resources/PreviewVideoDecoderTest.cpp
)


target_include_directories(
    DesktopSceneEngine_PreviewVideoDecoderTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_PreviewVideoDecoderTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_PreviewVideoDecoderTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_PreviewVideoDecoderTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR="${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
)


target_link_libraries(
    DesktopSceneEngine_PreviewVideoDecoderTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_PreviewVideoDecoderTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_PreviewVideoDecoderTest

    COMMAND

    DesktopSceneEngine_PreviewVideoDecoderTest
)

endif()


if(DSE_EXTERNAL_FIXTURES_AVAILABLE)

add_executable(
    DesktopSceneEngine_TimelinePreviewProviderTest
)


target_sources(
    DesktopSceneEngine_TimelinePreviewProviderTest
    PRIVATE

    Player/TimelinePreviewProviderTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
)


target_include_directories(
    DesktopSceneEngine_TimelinePreviewProviderTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_TimelinePreviewProviderTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_TimelinePreviewProviderTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_TimelinePreviewProviderTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR="${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
)


target_link_libraries(
    DesktopSceneEngine_TimelinePreviewProviderTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_TimelinePreviewProviderTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_TimelinePreviewProviderTest

    COMMAND

    DesktopSceneEngine_TimelinePreviewProviderTest
)

endif()


add_executable(
    DesktopSceneEngine_TimelineHoverGeometryTest
)


target_sources(
    DesktopSceneEngine_TimelineHoverGeometryTest
    PRIVATE

    Player/TimelineHoverGeometryTest.cpp
)


target_include_directories(
    DesktopSceneEngine_TimelineHoverGeometryTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_TimelineHoverGeometryTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_TimelineHoverGeometryTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_TimelineHoverGeometryTest
    PRIVATE

    Qt6::Widgets
)


add_test(
    NAME

    DesktopSceneEngine_TimelineHoverGeometryTest

    COMMAND

    DesktopSceneEngine_TimelineHoverGeometryTest
)


add_executable(
    DesktopSceneEngine_TimelineRepresentativeTimestampTest
)


target_sources(
    DesktopSceneEngine_TimelineRepresentativeTimestampTest
    PRIVATE

    Player/TimelineRepresentativeTimestampTest.cpp
)


target_include_directories(
    DesktopSceneEngine_TimelineRepresentativeTimestampTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_TimelineRepresentativeTimestampTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_TimelineRepresentativeTimestampTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_TimelineRepresentativeTimestampTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_TimelineRepresentativeTimestampTest

    COMMAND

    DesktopSceneEngine_TimelineRepresentativeTimestampTest
)


add_executable(
    DesktopSceneEngine_TimelinePreviewPreloadPlannerTest
)


target_sources(
    DesktopSceneEngine_TimelinePreviewPreloadPlannerTest
    PRIVATE

    Player/TimelinePreviewPreloadPlannerTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
)


target_include_directories(
    DesktopSceneEngine_TimelinePreviewPreloadPlannerTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_TimelinePreviewPreloadPlannerTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_TimelinePreviewPreloadPlannerTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_TimelinePreviewPreloadPlannerTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


add_test(
    NAME

    DesktopSceneEngine_TimelinePreviewPreloadPlannerTest

    COMMAND

    DesktopSceneEngine_TimelinePreviewPreloadPlannerTest
)


if(DSE_EXTERNAL_FIXTURES_AVAILABLE)

add_executable(
    DesktopSceneEngine_TimelinePreviewWorkerTest
)


target_sources(
    DesktopSceneEngine_TimelinePreviewWorkerTest
    PRIVATE

    Player/TimelinePreviewWorkerTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.h
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
)


set_target_properties(
    DesktopSceneEngine_TimelinePreviewWorkerTest
    PROPERTIES

    AUTOMOC ON
)


target_include_directories(
    DesktopSceneEngine_TimelinePreviewWorkerTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_TimelinePreviewWorkerTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_TimelinePreviewWorkerTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_TimelinePreviewWorkerTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR="${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
)


target_link_libraries(
    DesktopSceneEngine_TimelinePreviewWorkerTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_TimelinePreviewWorkerTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_TimelinePreviewWorkerTest

    COMMAND

    DesktopSceneEngine_TimelinePreviewWorkerTest
)

endif()


add_executable(
    DesktopSceneEngine_TimelinePreviewPresentationTest
)


target_sources(
    DesktopSceneEngine_TimelinePreviewPresentationTest
    PRIVATE

    Player/TimelinePreviewPresentationTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
)


target_include_directories(
    DesktopSceneEngine_TimelinePreviewPresentationTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_TimelinePreviewPresentationTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_TimelinePreviewPresentationTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_TimelinePreviewPresentationTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


add_test(
    NAME

    DesktopSceneEngine_TimelinePreviewPresentationTest

    COMMAND

    DesktopSceneEngine_TimelinePreviewPresentationTest
)


add_executable(
    DesktopSceneEngine_DisplayTest
)


target_sources(
    DesktopSceneEngine_DisplayTest
    PRIVATE

    Platform/DisplayTest.cpp
)


target_compile_features(
    DesktopSceneEngine_DisplayTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_DisplayTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_DisplayTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_DisplayTest

    COMMAND

    DesktopSceneEngine_DisplayTest
)


add_executable(
    DesktopSceneEngine_DisplaySelectionTest
)


target_sources(
    DesktopSceneEngine_DisplaySelectionTest
    PRIVATE

    Player/DisplaySelectionTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
)


target_include_directories(
    DesktopSceneEngine_DisplaySelectionTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_DisplaySelectionTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_DisplaySelectionTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_DisplaySelectionTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_DisplaySelectionTest

    COMMAND

    DesktopSceneEngine_DisplaySelectionTest
)


add_executable(
    DesktopSceneEngine_DisplayIdentifyTest
)


target_sources(
    DesktopSceneEngine_DisplayIdentifyTest
    PRIVATE

    Platform/DisplayIdentifyTest.cpp
)


target_compile_features(
    DesktopSceneEngine_DisplayIdentifyTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_DisplayIdentifyTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_DisplayIdentifyTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_DisplayIdentifyTest

    COMMAND

    DesktopSceneEngine_DisplayIdentifyTest
)


add_executable(
    DesktopSceneEngine_ScenePresentationMathTest
)


target_sources(
    DesktopSceneEngine_ScenePresentationMathTest
    PRIVATE

    Player/ScenePresentationMathTest.cpp
)


target_include_directories(
    DesktopSceneEngine_ScenePresentationMathTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/src/Core
    ${CMAKE_SOURCE_DIR}/Engine/src/Rendering
)


target_compile_features(
    DesktopSceneEngine_ScenePresentationMathTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_ScenePresentationMathTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_test(
    NAME

    DesktopSceneEngine_ScenePresentationMathTest

    COMMAND

    DesktopSceneEngine_ScenePresentationMathTest
)


add_executable(
    DesktopSceneEngine_PresentationGeometryTest
)


target_sources(
    DesktopSceneEngine_PresentationGeometryTest
    PRIVATE

    Core/PresentationGeometryTest.cpp
)


target_include_directories(
    DesktopSceneEngine_PresentationGeometryTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/include
)


target_compile_features(
    DesktopSceneEngine_PresentationGeometryTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_PresentationGeometryTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_test(
    NAME

    DesktopSceneEngine_PresentationGeometryTest

    COMMAND

    DesktopSceneEngine_PresentationGeometryTest
)


if(WIN32)

    add_executable(DesktopSceneEngine_DesktopShellTopologyTest
        Platform/DesktopShellTopologyTest.cpp)
    target_include_directories(DesktopSceneEngine_DesktopShellTopologyTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src)
    target_link_libraries(DesktopSceneEngine_DesktopShellTopologyTest
        PRIVATE DesktopSceneEngine::Engine)
    add_test(NAME DesktopSceneEngine_DesktopShellTopologyTest
        COMMAND DesktopSceneEngine_DesktopShellTopologyTest)

    add_executable(DesktopSceneEngine_DesktopPresentationHostTest
        Platform/DesktopPresentationHostTest.cpp
        Platform/DesktopPresentationHost.manifest)
    target_include_directories(DesktopSceneEngine_DesktopPresentationHostTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src)
    target_link_libraries(DesktopSceneEngine_DesktopPresentationHostTest
        PRIVATE DesktopSceneEngine::Engine user32)
    add_test(NAME DesktopSceneEngine_DesktopPresentationHostTest
        COMMAND DesktopSceneEngine_DesktopPresentationHostTest)

    add_executable(DesktopSceneEngine_DesktopPresentationHostIntegrationTest
        Platform/DesktopPresentationHostIntegrationTest.cpp
        Platform/DesktopPresentationHost.manifest)
    target_include_directories(DesktopSceneEngine_DesktopPresentationHostIntegrationTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src)
    target_link_libraries(DesktopSceneEngine_DesktopPresentationHostIntegrationTest
        PRIVATE DesktopSceneEngine::Engine user32)
    add_test(NAME DesktopSceneEngine_DesktopPresentationHostIntegrationTest
        COMMAND DesktopSceneEngine_DesktopPresentationHostIntegrationTest)

    add_executable(
        DesktopSceneEngine_NativeWindowClientSizeTest
    )


    target_sources(
        DesktopSceneEngine_NativeWindowClientSizeTest
        PRIVATE

        Platform/NativeWindowClientSizeTest.cpp
    )


    target_include_directories(
        DesktopSceneEngine_NativeWindowClientSizeTest
        PRIVATE

        ${CMAKE_SOURCE_DIR}/Engine/src
    )


    target_compile_features(
        DesktopSceneEngine_NativeWindowClientSizeTest
        PRIVATE

        cxx_std_20
    )


    target_compile_options(
        DesktopSceneEngine_NativeWindowClientSizeTest
        PRIVATE

        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )


    target_link_libraries(
        DesktopSceneEngine_NativeWindowClientSizeTest
        PRIVATE

        DesktopSceneEngine::Engine
        user32
    )


    add_test(
        NAME

        DesktopSceneEngine_NativeWindowClientSizeTest

        COMMAND

        DesktopSceneEngine_NativeWindowClientSizeTest
    )


    add_executable(
        DesktopSceneEngine_GraphicsBackendSingleTargetTest
    )


    target_sources(
        DesktopSceneEngine_GraphicsBackendSingleTargetTest
        PRIVATE

        Rendering/GraphicsBackendSingleTargetTest.cpp
    )


    target_include_directories(
        DesktopSceneEngine_GraphicsBackendSingleTargetTest
        PRIVATE

        ${CMAKE_SOURCE_DIR}/Engine/src/Rendering
    )


    target_compile_features(
        DesktopSceneEngine_GraphicsBackendSingleTargetTest
        PRIVATE

        cxx_std_20
    )


    target_compile_options(
        DesktopSceneEngine_GraphicsBackendSingleTargetTest
        PRIVATE

        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )


    target_link_libraries(
        DesktopSceneEngine_GraphicsBackendSingleTargetTest
        PRIVATE

        DesktopSceneEngine::Engine
        user32
    )


    add_test(
        NAME

        DesktopSceneEngine_GraphicsBackendSingleTargetTest

        COMMAND

        DesktopSceneEngine_GraphicsBackendSingleTargetTest
    )


    add_executable(
        DesktopSceneEngine_GraphicsBackendMultipleTargetTest
    )


    target_sources(
        DesktopSceneEngine_GraphicsBackendMultipleTargetTest
        PRIVATE

        Rendering/GraphicsBackendMultipleTargetTest.cpp
    )


    target_include_directories(
        DesktopSceneEngine_GraphicsBackendMultipleTargetTest
        PRIVATE

        ${CMAKE_SOURCE_DIR}/Engine/src/Rendering
    )


    target_compile_features(
        DesktopSceneEngine_GraphicsBackendMultipleTargetTest
        PRIVATE

        cxx_std_20
    )


    target_compile_options(
        DesktopSceneEngine_GraphicsBackendMultipleTargetTest
        PRIVATE

        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )


    target_link_libraries(
        DesktopSceneEngine_GraphicsBackendMultipleTargetTest
        PRIVATE

        DesktopSceneEngine::Engine
        user32
    )


    add_test(
        NAME

        DesktopSceneEngine_GraphicsBackendMultipleTargetTest

        COMMAND

        DesktopSceneEngine_GraphicsBackendMultipleTargetTest
    )

    add_executable(DesktopSceneEngine_GraphicsBackendCompositionTargetTest
        Rendering/GraphicsBackendCompositionTargetTest.cpp)
    target_include_directories(DesktopSceneEngine_GraphicsBackendCompositionTargetTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src/Rendering)
    target_compile_features(DesktopSceneEngine_GraphicsBackendCompositionTargetTest
        PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_GraphicsBackendCompositionTargetTest
        PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
                $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_GraphicsBackendCompositionTargetTest
        PRIVATE DesktopSceneEngine::Engine user32 dcomp)
    add_test(NAME DesktopSceneEngine_GraphicsBackendCompositionTargetTest
        COMMAND DesktopSceneEngine_GraphicsBackendCompositionTargetTest)

    add_executable(DesktopSceneEngine_CanvasTargetFitTest
        Rendering/CanvasTargetFitTest.cpp)
    target_include_directories(DesktopSceneEngine_CanvasTargetFitTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src/Rendering)
    target_compile_features(DesktopSceneEngine_CanvasTargetFitTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_CanvasTargetFitTest
        PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
                $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_CanvasTargetFitTest
        PRIVATE DesktopSceneEngine::Engine user32)
    add_test(NAME DesktopSceneEngine_CanvasTargetFitTest
        COMMAND DesktopSceneEngine_CanvasTargetFitTest)

endif()

add_executable(DesktopSceneEngine_PresentationViewportTest
    Rendering/PresentationViewportTest.cpp)
target_include_directories(DesktopSceneEngine_PresentationViewportTest
    PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src/Rendering)
target_compile_features(DesktopSceneEngine_PresentationViewportTest PRIVATE cxx_std_20)
target_compile_options(DesktopSceneEngine_PresentationViewportTest
    PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>)
add_test(NAME DesktopSceneEngine_PresentationViewportTest
    COMMAND DesktopSceneEngine_PresentationViewportTest)


add_executable(
    DesktopSceneEngine_AuthoredProjectTest
)


target_sources(
    DesktopSceneEngine_AuthoredProjectTest
    PRIVATE

    Domain/AuthoredProjectTest.cpp
)


target_include_directories(
    DesktopSceneEngine_AuthoredProjectTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/include
)


target_compile_features(
    DesktopSceneEngine_AuthoredProjectTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_AuthoredProjectTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_AuthoredProjectTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_AuthoredProjectTest

    COMMAND

    DesktopSceneEngine_AuthoredProjectTest
)


add_executable(
    DesktopSceneEngine_DisplayOwnerTest
)


target_sources(
    DesktopSceneEngine_DisplayOwnerTest
    PRIVATE

    Domain/DisplayOwnerTest.cpp
)


target_compile_features(
    DesktopSceneEngine_DisplayOwnerTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_DisplayOwnerTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_DisplayOwnerTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_DisplayOwnerTest

    COMMAND

    DesktopSceneEngine_DisplayOwnerTest
)


add_executable(
    DesktopSceneEngine_InspectorDeferredRefreshTest
)


target_sources(
    DesktopSceneEngine_InspectorDeferredRefreshTest
    PRIVATE

    Player/InspectorDeferredRefreshTest.cpp
)


target_compile_features(
    DesktopSceneEngine_InspectorDeferredRefreshTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_InspectorDeferredRefreshTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_InspectorDeferredRefreshTest
    PRIVATE

    Qt6::Widgets
)


add_test(
    NAME

    DesktopSceneEngine_InspectorDeferredRefreshTest

    COMMAND

    DesktopSceneEngine_InspectorDeferredRefreshTest
)


add_executable(
    DesktopSceneEngine_CanvasChromeFrameTest
)


target_sources(
    DesktopSceneEngine_CanvasChromeFrameTest
    PRIVATE

    Player/CanvasChromeFrameTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
)


target_include_directories(
    DesktopSceneEngine_CanvasChromeFrameTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_CanvasChromeFrameTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_CanvasChromeFrameTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_CanvasChromeFrameTest
    PRIVATE

    Qt6::Widgets
)


add_test(
    NAME

    DesktopSceneEngine_CanvasChromeFrameTest

    COMMAND

    DesktopSceneEngine_CanvasChromeFrameTest
)


add_executable(
    DesktopSceneEngine_RepresentativeFrameCacheContractTest
)


target_sources(
    DesktopSceneEngine_RepresentativeFrameCacheContractTest
    PRIVATE

    Resources/RepresentativeFrameCacheContractTest.cpp
)


target_include_directories(
    DesktopSceneEngine_RepresentativeFrameCacheContractTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_RepresentativeFrameCacheContractTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_RepresentativeFrameCacheContractTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_RepresentativeFrameCacheContractTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_RepresentativeFrameCacheContractTest

    COMMAND

    DesktopSceneEngine_RepresentativeFrameCacheContractTest
)


add_executable(
    DesktopSceneEngine_RepresentativeFramePngTest
)


target_sources(
    DesktopSceneEngine_RepresentativeFramePngTest
    PRIVATE

    Resources/RepresentativeFramePngTest.cpp
)


target_include_directories(
    DesktopSceneEngine_RepresentativeFramePngTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_RepresentativeFramePngTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_RepresentativeFramePngTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_RepresentativeFramePngTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_RepresentativeFramePngTest

    COMMAND

    DesktopSceneEngine_RepresentativeFramePngTest
)


add_executable(
    DesktopSceneEngine_RepresentativeFrameStoreTest
)


target_sources(
    DesktopSceneEngine_RepresentativeFrameStoreTest
    PRIVATE

    Resources/RepresentativeFrameStoreTest.cpp
)


target_include_directories(
    DesktopSceneEngine_RepresentativeFrameStoreTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_RepresentativeFrameStoreTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_RepresentativeFrameStoreTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_RepresentativeFrameStoreTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_RepresentativeFrameStoreTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_RepresentativeFrameStoreTest

    COMMAND

    DesktopSceneEngine_RepresentativeFrameStoreTest
)


if(DSE_EXTERNAL_FIXTURES_AVAILABLE)

add_executable(
    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest
)


target_sources(
    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest
    PRIVATE

    Resources/RepresentativeFrameStoreIntegrationTest.cpp
)


target_include_directories(
    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR="${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
)


target_link_libraries(
    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest

    COMMAND

    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest
)

endif()


add_executable(
    DesktopSceneEngine_BackgroundMusicWindowTest
)


target_sources(
    DesktopSceneEngine_BackgroundMusicWindowTest
    PRIVATE

    Player/BackgroundMusicWindowTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


target_include_directories(
    DesktopSceneEngine_BackgroundMusicWindowTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/include
)


target_compile_features(
    DesktopSceneEngine_BackgroundMusicWindowTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_BackgroundMusicWindowTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_BackgroundMusicWindowTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


add_test(
    NAME

    DesktopSceneEngine_BackgroundMusicWindowTest

    COMMAND

    DesktopSceneEngine_BackgroundMusicWindowTest
)


add_executable(
    DesktopSceneEngine_MainWindowBackgroundMusicLifecycleTest
)


target_sources(
    DesktopSceneEngine_MainWindowBackgroundMusicLifecycleTest
    PRIVATE

    Player/MainWindowBackgroundMusicLifecycleTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.h
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
    ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
    ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
    ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
    ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


set_target_properties(
    DesktopSceneEngine_MainWindowBackgroundMusicLifecycleTest
    PROPERTIES

    AUTOMOC ON
)


target_include_directories(
    DesktopSceneEngine_MainWindowBackgroundMusicLifecycleTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_MainWindowBackgroundMusicLifecycleTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_MainWindowBackgroundMusicLifecycleTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_MainWindowBackgroundMusicLifecycleTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


add_test(
    NAME

    DesktopSceneEngine_MainWindowBackgroundMusicLifecycleTest

    COMMAND

    DesktopSceneEngine_MainWindowBackgroundMusicLifecycleTest
)


if(DSE_EXTERNAL_FIXTURES_AVAILABLE)

add_executable(
    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
)


target_sources(
    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
    PRIVATE

    Player/MainWindowBackgroundMusicForwardingTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.h
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
    ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
    ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
    ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
    ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


set_target_properties(
    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
    PROPERTIES

    AUTOMOC ON
)


target_include_directories(
    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR="${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
)


target_link_libraries(
    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest

    COMMAND

    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
)

endif()


if(DSE_EXTERNAL_FIXTURES_AVAILABLE)

add_executable(
    DesktopSceneEngine_MainWindowPlaybackSequenceTest
)


target_sources(
    DesktopSceneEngine_MainWindowPlaybackSequenceTest
    PRIVATE

    Player/MainWindowPlaybackSequenceTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.h
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
    ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
    ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
    ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
    ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


set_target_properties(
    DesktopSceneEngine_MainWindowPlaybackSequenceTest
    PROPERTIES

    AUTOMOC ON
)


target_include_directories(
    DesktopSceneEngine_MainWindowPlaybackSequenceTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_MainWindowPlaybackSequenceTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_MainWindowPlaybackSequenceTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_compile_definitions(
    DesktopSceneEngine_MainWindowPlaybackSequenceTest
    PRIVATE

    DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR="${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
)


target_link_libraries(
    DesktopSceneEngine_MainWindowPlaybackSequenceTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_MainWindowPlaybackSequenceTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_MainWindowPlaybackSequenceTest

    COMMAND

    DesktopSceneEngine_MainWindowPlaybackSequenceTest
)

endif()


add_executable(
    DesktopSceneEngine_MainWindowPlaybackModeUiTest
)


target_sources(
    DesktopSceneEngine_MainWindowPlaybackModeUiTest
    PRIVATE

    Player/MainWindowPlaybackModeUiTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.h
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
    ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
    ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
    ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
    ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


set_target_properties(
    DesktopSceneEngine_MainWindowPlaybackModeUiTest
    PROPERTIES

    AUTOMOC ON
)


target_include_directories(
    DesktopSceneEngine_MainWindowPlaybackModeUiTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_MainWindowPlaybackModeUiTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_MainWindowPlaybackModeUiTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_dependencies(
    DesktopSceneEngine_MainWindowPlaybackModeUiTest

    DesktopSceneEngine_QtPlayer_lrelease
)


add_custom_command(
    TARGET DesktopSceneEngine_MainWindowPlaybackModeUiTest
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DesktopSceneEngine_MainWindowPlaybackModeUiTest>/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.qm"
        $<TARGET_FILE_DIR:DesktopSceneEngine_MainWindowPlaybackModeUiTest>/i18n
    VERBATIM
)


target_link_libraries(
    DesktopSceneEngine_MainWindowPlaybackModeUiTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_MainWindowPlaybackModeUiTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_MainWindowPlaybackModeUiTest

    COMMAND

    DesktopSceneEngine_MainWindowPlaybackModeUiTest
)


add_executable(
    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest
)


target_sources(
    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest
    PRIVATE

    Player/MainWindowAvailabilityOrderingUiTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.h
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
    ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
    ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
    ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
    ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


set_target_properties(
    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest
    PROPERTIES

    AUTOMOC ON
)


target_include_directories(
    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_dependencies(
    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest

    DesktopSceneEngine_QtPlayer_lrelease
)


add_custom_command(
    TARGET DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest>/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.qm"
        $<TARGET_FILE_DIR:DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest>/i18n
    VERBATIM
)


target_link_libraries(
    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest

    COMMAND

    DesktopSceneEngine_MainWindowAvailabilityOrderingUiTest
)


add_executable(
    DesktopSceneEngine_MainWindowDocumentLifecycleTest
)


target_sources(
    DesktopSceneEngine_MainWindowDocumentLifecycleTest
    PRIVATE

    Player/MainWindowDocumentLifecycleTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.h
    ${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
    ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
    ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
    ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
    ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
    ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
    ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
    ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp
)


set_target_properties(
    DesktopSceneEngine_MainWindowDocumentLifecycleTest
    PROPERTIES

    AUTOMOC ON
)


target_include_directories(
    DesktopSceneEngine_MainWindowDocumentLifecycleTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_MainWindowDocumentLifecycleTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_MainWindowDocumentLifecycleTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_dependencies(
    DesktopSceneEngine_MainWindowDocumentLifecycleTest

    DesktopSceneEngine_QtPlayer_lrelease
)


add_custom_command(
    TARGET DesktopSceneEngine_MainWindowDocumentLifecycleTest
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DesktopSceneEngine_MainWindowDocumentLifecycleTest>/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.qm"
        $<TARGET_FILE_DIR:DesktopSceneEngine_MainWindowDocumentLifecycleTest>/i18n
    VERBATIM
)


target_link_libraries(
    DesktopSceneEngine_MainWindowDocumentLifecycleTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


if(WIN32)

    deploy_ffmpeg_dlls(DesktopSceneEngine_MainWindowDocumentLifecycleTest)

endif()


add_test(
    NAME

    DesktopSceneEngine_MainWindowDocumentLifecycleTest

    COMMAND

    DesktopSceneEngine_MainWindowDocumentLifecycleTest
)


add_executable(
    DesktopSceneEngine_ApplicationPreferencesTest
)


target_sources(
    DesktopSceneEngine_ApplicationPreferencesTest
    PRIVATE

    Player/ApplicationPreferencesTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
)


target_include_directories(
    DesktopSceneEngine_ApplicationPreferencesTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_ApplicationPreferencesTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_ApplicationPreferencesTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_ApplicationPreferencesTest
    PRIVATE

    Qt6::Core
    DesktopSceneEngine::Engine
)


add_dependencies(
    DesktopSceneEngine_ApplicationPreferencesTest

    DesktopSceneEngine_QtPlayer_lrelease
)


add_custom_command(
    TARGET DesktopSceneEngine_ApplicationPreferencesTest
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DesktopSceneEngine_ApplicationPreferencesTest>/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.qm"
        $<TARGET_FILE_DIR:DesktopSceneEngine_ApplicationPreferencesTest>/i18n
    VERBATIM
)


add_test(
    NAME

    DesktopSceneEngine_ApplicationPreferencesTest

    COMMAND

    DesktopSceneEngine_ApplicationPreferencesTest
)


add_executable(
    DesktopSceneEngine_BackgroundMusicRuntimeTest
)


target_sources(
    DesktopSceneEngine_BackgroundMusicRuntimeTest
    PRIVATE

    Music/BackgroundMusicRuntimeTest.cpp
)


target_include_directories(
    DesktopSceneEngine_BackgroundMusicRuntimeTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_BackgroundMusicRuntimeTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_BackgroundMusicRuntimeTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_BackgroundMusicRuntimeTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_BackgroundMusicRuntimeTest

    COMMAND

    DesktopSceneEngine_BackgroundMusicRuntimeTest
)


add_executable(
    DesktopSceneEngine_BackgroundMusicOrchestratorTest
)


target_sources(
    DesktopSceneEngine_BackgroundMusicOrchestratorTest
    PRIVATE

    Music/BackgroundMusicOrchestratorTest.cpp
)


target_include_directories(
    DesktopSceneEngine_BackgroundMusicOrchestratorTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_BackgroundMusicOrchestratorTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_BackgroundMusicOrchestratorTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_BackgroundMusicOrchestratorTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_BackgroundMusicOrchestratorTest

    COMMAND

    DesktopSceneEngine_BackgroundMusicOrchestratorTest
)


add_executable(
    DesktopSceneEngine_MusicSessionTest
)


target_sources(
    DesktopSceneEngine_MusicSessionTest
    PRIVATE

    Music/MusicSessionTest.cpp
)


target_include_directories(
    DesktopSceneEngine_MusicSessionTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_MusicSessionTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_MusicSessionTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_MusicSessionTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_MusicSessionTest

    COMMAND

    DesktopSceneEngine_MusicSessionTest
)


add_executable(
    DesktopSceneEngine_MusicSessionReconciliationTest
)


target_sources(
    DesktopSceneEngine_MusicSessionReconciliationTest
    PRIVATE

    Music/MusicSessionReconciliationTest.cpp
)


target_include_directories(
    DesktopSceneEngine_MusicSessionReconciliationTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_MusicSessionReconciliationTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_MusicSessionReconciliationTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_MusicSessionReconciliationTest
    PRIVATE

    DesktopSceneEngine::Engine
)


add_test(
    NAME

    DesktopSceneEngine_MusicSessionReconciliationTest

    COMMAND

    DesktopSceneEngine_MusicSessionReconciliationTest
)


add_executable(
    DesktopSceneEngine_PlaybackSequenceControllerTest
)


target_sources(
    DesktopSceneEngine_PlaybackSequenceControllerTest
    PRIVATE

    Player/PlaybackSequenceControllerTest.cpp
)


target_include_directories(
    DesktopSceneEngine_PlaybackSequenceControllerTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_PlaybackSequenceControllerTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_PlaybackSequenceControllerTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


add_test(
    NAME

    DesktopSceneEngine_PlaybackSequenceControllerTest

    COMMAND

    DesktopSceneEngine_PlaybackSequenceControllerTest
)


add_executable(
    DesktopSceneEngine_DisplaySessionBootstrapTest
)


target_sources(
    DesktopSceneEngine_DisplaySessionBootstrapTest
    PRIVATE

    Player/DisplaySessionBootstrapTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/DisplaySessionBootstrap.cpp
)


target_include_directories(
    DesktopSceneEngine_DisplaySessionBootstrapTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_DisplaySessionBootstrapTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_DisplaySessionBootstrapTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_DisplaySessionBootstrapTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Core
)


add_test(
    NAME

    DesktopSceneEngine_DisplaySessionBootstrapTest

    COMMAND

    DesktopSceneEngine_DisplaySessionBootstrapTest
)


add_executable(
    DesktopSceneEngine_LauncherWindowTest
)


target_sources(
    DesktopSceneEngine_LauncherWindowTest
    PRIVATE

    Player/LauncherWindowTest.cpp
    ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
    ${CMAKE_SOURCE_DIR}/Player/DisplaySessionBootstrap.cpp
    ${CMAKE_SOURCE_DIR}/Player/LauncherWindow.cpp
)


target_include_directories(
    DesktopSceneEngine_LauncherWindowTest
    PRIVATE

    ${CMAKE_SOURCE_DIR}/Player
)


target_compile_features(
    DesktopSceneEngine_LauncherWindowTest
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_LauncherWindowTest
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_LauncherWindowTest
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
)


add_test(
    NAME

    DesktopSceneEngine_LauncherWindowTest

    COMMAND

    DesktopSceneEngine_LauncherWindowTest
)

if(WIN32)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Network)

    # Explicit-build-only measurement apparatus. Never a registered CTest soak
    # or a dependency of the default test build; no Qt/Player runtime workload.
    add_executable(DesktopSceneEngine_EngineGraphicsSoak EXCLUDE_FROM_ALL
        Performance/EngineGraphicsSoak.cpp)
    target_include_directories(DesktopSceneEngine_EngineGraphicsSoak
        PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_EngineGraphicsSoak PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_EngineGraphicsSoak
        PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus> $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_compile_definitions(DesktopSceneEngine_EngineGraphicsSoak
        PRIVATE SOAK_BUILD_CONFIGURATION="$<CONFIG>")
    target_link_libraries(DesktopSceneEngine_EngineGraphicsSoak
        PRIVATE DesktopSceneEngine::Engine nlohmann_json::nlohmann_json user32)
    deploy_ffmpeg_dlls(DesktopSceneEngine_EngineGraphicsSoak)

    add_executable(DesktopSceneEngine_InstanceAdmissionActivationTest
        Player/InstanceAdmissionActivationTest.cpp
        ${CMAKE_SOURCE_DIR}/Player/InstanceAdmission.cpp
        ${CMAKE_SOURCE_DIR}/Player/InstanceActivation.cpp)
    target_include_directories(DesktopSceneEngine_InstanceAdmissionActivationTest PRIVATE ${CMAKE_SOURCE_DIR}/Player)
    target_compile_features(DesktopSceneEngine_InstanceAdmissionActivationTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_InstanceAdmissionActivationTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_InstanceAdmissionActivationTest
        PRIVATE DesktopSceneEngine::Engine Qt6::Widgets Qt6::Network advapi32 user32)
    foreach(case keys ownership errors abandoned startup-gap startup-timeout protocol capacity native-modal native-modal-nested shutdown ready-gap busy-pipe)
        add_test(NAME DesktopSceneEngine_Instance_${case} COMMAND DesktopSceneEngine_InstanceAdmissionActivationTest ${case})
        set_tests_properties(DesktopSceneEngine_Instance_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()

    if(TARGET DesktopSceneEngine_QtPlayer)
        add_executable(DesktopSceneEngine_ApplicationIconTest
            Player/ApplicationIconTest.cpp
            ${CMAKE_SOURCE_DIR}/Player/DisplaySessionBootstrap.cpp)
        target_include_directories(DesktopSceneEngine_ApplicationIconTest PRIVATE ${CMAKE_SOURCE_DIR}/Player)
        target_compile_features(DesktopSceneEngine_ApplicationIconTest PRIVATE cxx_std_20)
        target_compile_options(DesktopSceneEngine_ApplicationIconTest
            PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
        target_compile_definitions(DesktopSceneEngine_ApplicationIconTest PRIVATE
            APPLICATION_ICON_PLAYER="$<TARGET_FILE:DesktopSceneEngine_QtPlayer>"
            APPLICATION_ICON_SOURCE="${CMAKE_SOURCE_DIR}/Player/resources/DesktopSceneEngine.ico")
        target_link_libraries(DesktopSceneEngine_ApplicationIconTest
            PRIVATE DesktopSceneEngine::Engine Qt6::Widgets user32)
        add_dependencies(DesktopSceneEngine_ApplicationIconTest DesktopSceneEngine_QtPlayer)
        add_test(NAME DesktopSceneEngine_ApplicationIconTest COMMAND DesktopSceneEngine_ApplicationIconTest)
        set_tests_properties(DesktopSceneEngine_ApplicationIconTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endif()

    add_executable(DesktopSceneEngine_InstanceProductionProcessTest
        Player/InstanceProductionProcessTest.cpp
        ${CMAKE_SOURCE_DIR}/Player/InstanceAdmission.cpp
        ${CMAKE_SOURCE_DIR}/Player/DisplaySessionBootstrap.cpp)
    target_include_directories(DesktopSceneEngine_InstanceProductionProcessTest PRIVATE ${CMAKE_SOURCE_DIR}/Player)
    target_compile_features(DesktopSceneEngine_InstanceProductionProcessTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_InstanceProductionProcessTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_compile_definitions(DesktopSceneEngine_InstanceProductionProcessTest
        PRIVATE P0_PLAYER_EXECUTABLE="$<TARGET_FILE:DesktopSceneEngine_QtPlayer>")
    target_link_libraries(DesktopSceneEngine_InstanceProductionProcessTest PRIVATE DesktopSceneEngine::Engine Qt6::Core user32 advapi32 ole32 oleaut32 uiautomationcore)
    add_dependencies(DesktopSceneEngine_InstanceProductionProcessTest DesktopSceneEngine_QtPlayer)
    foreach(case launcher session launcher-race session-race different restart crash launcher-crash launcher-crash-bootstrap launcher-crash-unobserved-teardown normal-close coordinator-lifetime coordinator-direct session-visibility hidden-close-exit)
        add_test(NAME DesktopSceneEngine_InstanceProduction_${case} COMMAND DesktopSceneEngine_InstanceProductionProcessTest ${case})
        set_tests_properties(DesktopSceneEngine_InstanceProduction_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)
    endforeach()

    add_executable(DesktopSceneEngine_ApplicationCoordinatorTest
        Player/ApplicationCoordinatorTest.cpp
        ${CMAKE_SOURCE_DIR}/Player/ApplicationCoordinator.cpp
        ${CMAKE_SOURCE_DIR}/Player/SessionPresentation.cpp
        ${CMAKE_SOURCE_DIR}/Player/InstanceActivation.cpp
        ${CMAKE_SOURCE_DIR}/Player/InstanceAdmission.cpp
        ${CMAKE_SOURCE_DIR}/Player/LauncherWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/DisplaySessionBootstrap.cpp
        ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp)
    target_include_directories(DesktopSceneEngine_ApplicationCoordinatorTest PRIVATE ${CMAKE_SOURCE_DIR}/Player)
    target_compile_features(DesktopSceneEngine_ApplicationCoordinatorTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_ApplicationCoordinatorTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_ApplicationCoordinatorTest
        PRIVATE DesktopSceneEngine::Engine Qt6::Widgets Qt6::Network advapi32 user32)
    deploy_ffmpeg_dlls(DesktopSceneEngine_ApplicationCoordinatorTest)
    foreach(case ui-lifetime protocol-lifetime bootstrap-timeout unavailable-join pending-teardown)
        add_test(NAME DesktopSceneEngine_Coordinator_${case} COMMAND DesktopSceneEngine_ApplicationCoordinatorTest ${case})
        set_tests_properties(DesktopSceneEngine_Coordinator_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()

    add_executable(DesktopSceneEngine_InstanceSessionLifecycleTest
        Player/InstanceSessionLifecycleTest.cpp
        ${CMAKE_SOURCE_DIR}/Player/ApplicationCoordinator.cpp
        ${CMAKE_SOURCE_DIR}/Player/Player.manifest
        ${CMAKE_SOURCE_DIR}/Player/MainWindow.h
        ${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
        ${CMAKE_SOURCE_DIR}/Player/LauncherWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/DisplaySessionBootstrap.cpp
        ${CMAKE_SOURCE_DIR}/Player/InstanceAdmission.cpp
        ${CMAKE_SOURCE_DIR}/Player/InstanceActivation.cpp
        ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
        ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
        ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
        ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
        ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
        ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp)
    set_target_properties(DesktopSceneEngine_InstanceSessionLifecycleTest PROPERTIES AUTOMOC ON)
    target_include_directories(DesktopSceneEngine_InstanceSessionLifecycleTest PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_InstanceSessionLifecycleTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_InstanceSessionLifecycleTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_compile_definitions(DesktopSceneEngine_InstanceSessionLifecycleTest PRIVATE P0_PLAYER_EXECUTABLE="$<TARGET_FILE:DesktopSceneEngine_QtPlayer>")
    target_link_libraries(DesktopSceneEngine_InstanceSessionLifecycleTest PRIVATE DesktopSceneEngine::Engine Qt6::Widgets Qt6::Network advapi32 user32)
    add_dependencies(DesktopSceneEngine_InstanceSessionLifecycleTest DesktopSceneEngine_QtPlayer)
    deploy_ffmpeg_dlls(DesktopSceneEngine_InstanceSessionLifecycleTest)
    add_test(NAME DesktopSceneEngine_InstanceSessionLifecycleTest COMMAND DesktopSceneEngine_InstanceSessionLifecycleTest)
    foreach(case tray-core tray-state tray-loss tray-expiry tray-epoch tray-stale tray-maximized tray-playing tray-modal tray-native-modal tray-unqualified tray-unavailable tray-retire-control tray-pending tray-ending tray-delayed-batch tray-expired-permit tray-backpressure)
        add_test(NAME DesktopSceneEngine_SessionPresentation_${case} COMMAND DesktopSceneEngine_InstanceSessionLifecycleTest ${case})
        set_tests_properties(DesktopSceneEngine_SessionPresentation_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()
    set_tests_properties(DesktopSceneEngine_InstanceSessionLifecycleTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

    get_target_property(tray_role_sources DesktopSceneEngine_InstanceSessionLifecycleTest SOURCES)
    list(REMOVE_ITEM tray_role_sources Player/InstanceSessionLifecycleTest.cpp)
    add_executable(DesktopSceneEngine_TrayRolePresentationTest Player/TrayRolePresentationTest.cpp ${tray_role_sources})
    set_target_properties(DesktopSceneEngine_TrayRolePresentationTest PROPERTIES AUTOMOC ON)
    target_include_directories(DesktopSceneEngine_TrayRolePresentationTest PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_TrayRolePresentationTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_TrayRolePresentationTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_TrayRolePresentationTest PRIVATE DesktopSceneEngine::Engine Qt6::Widgets Qt6::Network advapi32 user32)
    deploy_ffmpeg_dlls(DesktopSceneEngine_TrayRolePresentationTest)
    add_dependencies(DesktopSceneEngine_TrayRolePresentationTest DesktopSceneEngine_QtPlayer_lrelease)
    add_custom_command(TARGET DesktopSceneEngine_TrayRolePresentationTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:DesktopSceneEngine_TrayRolePresentationTest>/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.qm"
            $<TARGET_FILE_DIR:DesktopSceneEngine_TrayRolePresentationTest>/i18n VERBATIM)
    foreach(case menu launcher all lifecycle ordering modal qualification snapshot terminal-loss replacement overlap unavailable nonactivation native-modal-all application-modal-all drain-join)
        add_test(NAME DesktopSceneEngine_TrayRoles_${case} COMMAND DesktopSceneEngine_TrayRolePresentationTest ${case})
        set_tests_properties(DesktopSceneEngine_TrayRoles_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()

    add_executable(DesktopSceneEngine_TrayControlPresentationTest Player/TrayControlPresentationTest.cpp ${tray_role_sources})
    set_target_properties(DesktopSceneEngine_TrayControlPresentationTest PROPERTIES AUTOMOC ON)
    target_include_directories(DesktopSceneEngine_TrayControlPresentationTest PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_TrayControlPresentationTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_TrayControlPresentationTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_TrayControlPresentationTest PRIVATE DesktopSceneEngine::Engine Qt6::Widgets Qt6::Network advapi32 user32)
    deploy_ffmpeg_dlls(DesktopSceneEngine_TrayControlPresentationTest)
    foreach(scale 1 1.5 2)
        add_test(NAME DesktopSceneEngine_TrayControl_dpi-${scale} COMMAND DesktopSceneEngine_TrayControlPresentationTest)
        set_tests_properties(DesktopSceneEngine_TrayControl_dpi-${scale} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60 ENVIRONMENT "QT_SCALE_FACTOR=${scale}")
    endforeach()

    add_executable(DesktopSceneEngine_GlobalLanguageSettingsTest
        Player/GlobalLanguageSettingsTest.cpp
        ${CMAKE_SOURCE_DIR}/Player/MainWindow.h
        ${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
        ${CMAKE_SOURCE_DIR}/Player/LauncherWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/DisplaySessionBootstrap.cpp
        ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
        ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
        ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
        ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
        ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
        ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp)
    set_target_properties(DesktopSceneEngine_GlobalLanguageSettingsTest PROPERTIES AUTOMOC ON)
    target_include_directories(DesktopSceneEngine_GlobalLanguageSettingsTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/tests/Player ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_GlobalLanguageSettingsTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_GlobalLanguageSettingsTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_GlobalLanguageSettingsTest PRIVATE DesktopSceneEngine::Engine Qt6::Widgets user32)
    add_dependencies(DesktopSceneEngine_GlobalLanguageSettingsTest DesktopSceneEngine_QtPlayer_lrelease)
    add_custom_command(TARGET DesktopSceneEngine_GlobalLanguageSettingsTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:DesktopSceneEngine_GlobalLanguageSettingsTest>/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.qm"
            $<TARGET_FILE_DIR:DesktopSceneEngine_GlobalLanguageSettingsTest>/i18n VERBATIM)
    deploy_ffmpeg_dlls(DesktopSceneEngine_GlobalLanguageSettingsTest)
    foreach(case launcher authority persistence rejected-commit partial-success native-persistence consumer-clean consumer-dirty startup queued-lifecycle processes)
        add_test(NAME DesktopSceneEngine_GlobalLanguage_${case} COMMAND DesktopSceneEngine_GlobalLanguageSettingsTest ${case})
    endforeach()

    add_executable(DesktopSceneEngine_MainWindowDesktopPresentationTest
        Player/MainWindowDesktopPresentationTest.cpp
        ${CMAKE_SOURCE_DIR}/Player/Player.manifest
        ${CMAKE_SOURCE_DIR}/Player/MainWindow.h
        ${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp
        ${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp
        ${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp
        ${CMAKE_SOURCE_DIR}/Player/PreviewViewportHost.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewProvider.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewWorker.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPreloadPlanner.cpp
        ${CMAKE_SOURCE_DIR}/Player/TimelinePreviewPresentation.cpp
        ${CMAKE_SOURCE_DIR}/Player/CanvasChromeFrame.cpp
        ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp
        ${CMAKE_SOURCE_DIR}/Player/BackgroundMusicWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp)
    set_target_properties(DesktopSceneEngine_MainWindowDesktopPresentationTest PROPERTIES AUTOMOC ON)
    target_include_directories(DesktopSceneEngine_MainWindowDesktopPresentationTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_MainWindowDesktopPresentationTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_MainWindowDesktopPresentationTest
        PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus> $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_MainWindowDesktopPresentationTest
        PRIVATE DesktopSceneEngine::Engine Qt6::Widgets Qt6::Network user32 advapi32)
    target_sources(DesktopSceneEngine_MainWindowDesktopPresentationTest PRIVATE
        ${CMAKE_SOURCE_DIR}/Player/ApplicationCoordinator.cpp
        ${CMAKE_SOURCE_DIR}/Player/LauncherWindow.cpp
        ${CMAKE_SOURCE_DIR}/Player/InstanceAdmission.cpp
        ${CMAKE_SOURCE_DIR}/Player/InstanceActivation.cpp
        ${CMAKE_SOURCE_DIR}/Player/DisplaySessionBootstrap.cpp)
    add_test(NAME DesktopSceneEngine_SessionPresentation_tray-deployed
        COMMAND DesktopSceneEngine_MainWindowDesktopPresentationTest --tray-deployed)
    foreach(case menu visible hidden stale disconnected minimized failure disconnect-flight revision detach sequential showall loss peer rapid busy translation unavailable unavailable-flight)
        add_test(NAME DesktopSceneEngine_TrayDeployment_${case}
            COMMAND DesktopSceneEngine_MainWindowDesktopPresentationTest --tray-deployment-${case})
        set_tests_properties(DesktopSceneEngine_TrayDeployment_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()
    set_tests_properties(DesktopSceneEngine_SessionPresentation_tray-deployed PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    foreach(case menu launcher-minimized launcher-hidden launcher-settings clean minimized-clean hidden-clean visible-cancel visible-save visible-discard hidden-save hidden-discard hidden-cancel save-as-cancel save-failure modal bgm-modal native-modal bgm-native-modal query query-old control-loss show-pending showall-pending duplicate pending-wait uncertain deploy-race peer-last peer-replacement peer-late peer-pending-death)
        add_test(NAME DesktopSceneEngine_TrayClose_${case}
            COMMAND DesktopSceneEngine_MainWindowDesktopPresentationTest --tray-close-${case})
        set_tests_properties(DesktopSceneEngine_TrayClose_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()
    add_dependencies(DesktopSceneEngine_MainWindowDesktopPresentationTest
        DesktopSceneEngine_QtPlayer_lrelease)
    add_custom_command(TARGET DesktopSceneEngine_MainWindowDesktopPresentationTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:DesktopSceneEngine_MainWindowDesktopPresentationTest>/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.qm"
            $<TARGET_FILE_DIR:DesktopSceneEngine_MainWindowDesktopPresentationTest>/i18n
        VERBATIM)
    deploy_ffmpeg_dlls(DesktopSceneEngine_MainWindowDesktopPresentationTest)
    add_test(NAME DesktopSceneEngine_MainWindowDesktopPresentationTest
        COMMAND DesktopSceneEngine_MainWindowDesktopPresentationTest)

    # Real independent Session children, ordinary Close and current coordinator.
    get_target_property(tray_close_all_sources DesktopSceneEngine_MainWindowDesktopPresentationTest SOURCES)
    list(REMOVE_ITEM tray_close_all_sources Player/MainWindowDesktopPresentationTest.cpp)
    add_executable(DesktopSceneEngine_TrayCloseAllTest Player/TrayCloseAllTest.cpp ${tray_close_all_sources})
    set_target_properties(DesktopSceneEngine_TrayCloseAllTest PROPERTIES AUTOMOC ON)
    target_include_directories(DesktopSceneEngine_TrayCloseAllTest PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_TrayCloseAllTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_TrayCloseAllTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus> $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_TrayCloseAllTest PRIVATE DesktopSceneEngine::Engine Qt6::Widgets Qt6::Network user32 advapi32)
    add_dependencies(DesktopSceneEngine_TrayCloseAllTest DesktopSceneEngine_QtPlayer_lrelease)
    add_custom_command(TARGET DesktopSceneEngine_TrayCloseAllTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:DesktopSceneEngine_TrayCloseAllTest>/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.qm"
            $<TARGET_FILE_DIR:DesktopSceneEngine_TrayCloseAllTest>/i18n VERBATIM)
    deploy_ffmpeg_dlls(DesktopSceneEngine_TrayCloseAllTest)
    foreach(case action order cancel later replacement busy pending uncertain all-abort save-as save-failure bgm-busy preexisting-close preexisting-request unavailable launcher-recreate pending-death ordinary-overlap old-result full-accept launcher-only discard save late-drain hidden translation)
        add_test(NAME DesktopSceneEngine_TrayCloseAll_${case} COMMAND DesktopSceneEngine_TrayCloseAllTest ${case})
        set_tests_properties(DesktopSceneEngine_TrayCloseAll_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()
endif()

if(TARGET DesktopSceneEngine_TrayCloseAllTest)
    get_target_property(tray_recovery_sources DesktopSceneEngine_TrayCloseAllTest SOURCES)
    list(REMOVE_ITEM tray_recovery_sources Player/TrayCloseAllTest.cpp)
    add_executable(DesktopSceneEngine_TrayRecoveryTest Player/TrayRecoveryTest.cpp ${tray_recovery_sources})
    set_target_properties(DesktopSceneEngine_TrayRecoveryTest PROPERTIES AUTOMOC ON)
    target_include_directories(DesktopSceneEngine_TrayRecoveryTest PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_TrayRecoveryTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_TrayRecoveryTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus> $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_TrayRecoveryTest PRIVATE DesktopSceneEngine::Engine Qt6::Widgets Qt6::Network user32 advapi32 ole32 oleaut32)
    add_dependencies(DesktopSceneEngine_TrayRecoveryTest DesktopSceneEngine_QtPlayer)
    qt_add_resources(DesktopSceneEngine_TrayRecoveryTest s7_application_icon
        PREFIX "/icons" BASE "${CMAKE_SOURCE_DIR}/Player/resources"
        FILES "${CMAKE_SOURCE_DIR}/Player/resources/DesktopSceneEngine.ico")
    deploy_ffmpeg_dlls(DesktopSceneEngine_TrayRecoveryTest)
    foreach(case death concurrent rejoin fencing relaunch shell shell-pending disconnect maximized-loss minimized-max-loss failed disconnect-death pending-death batch-death production-recovery gate-diagnostics prior-key-terminal prior-key-pending prior-key-fencing)
        add_test(NAME DesktopSceneEngine_TrayRecovery_${case} COMMAND DesktopSceneEngine_TrayRecoveryTest ${case})
        set_tests_properties(DesktopSceneEngine_TrayRecovery_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 65)
    endforeach()
endif()

# Every existing MainWindow fixture builds the same local presentation implementation.
get_property(session_fixture_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(session_fixture_target IN LISTS session_fixture_targets)
    get_target_property(session_fixture_sources ${session_fixture_target} SOURCES)
    list(FIND session_fixture_sources "${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp" session_main_source)
    list(FIND session_fixture_sources "${CMAKE_SOURCE_DIR}/Player/LauncherWindow.cpp" session_launcher_source)
    list(FIND session_fixture_sources "${CMAKE_SOURCE_DIR}/Player/SessionPresentation.cpp" session_presentation_source)
    if((NOT session_main_source EQUAL -1 OR NOT session_launcher_source EQUAL -1) AND session_presentation_source EQUAL -1)
        target_sources(${session_fixture_target} PRIVATE ${CMAKE_SOURCE_DIR}/Player/SessionPresentation.cpp)
    endif()
    if(NOT session_launcher_source EQUAL -1)
        target_sources(${session_fixture_target} PRIVATE ${CMAKE_SOURCE_DIR}/Player/DisplayPresentation.cpp)
    endif()
    if(NOT session_main_source EQUAL -1 OR NOT session_launcher_source EQUAL -1)
        # S5 hidden ordinary-Close interactions reuse the existing activation helper.
        # Compile that same helper in legacy local fixtures, not a test-only substitute.
        foreach(session_activation_source InstanceActivation.cpp InstanceAdmission.cpp)
            list(FIND session_fixture_sources "${CMAKE_SOURCE_DIR}/Player/${session_activation_source}" session_activation_index)
            if(session_activation_index EQUAL -1)
                target_sources(${session_fixture_target} PRIVATE "${CMAKE_SOURCE_DIR}/Player/${session_activation_source}")
            endif()
        endforeach()
        target_link_libraries(${session_fixture_target} PRIVATE Qt6::Network advapi32 user32)
    endif()
endforeach()
