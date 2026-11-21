include("${CMAKE_CURRENT_LIST_DIR}/cmake/ExternalFixtures.cmake")

add_test(NAME DesktopSceneEngine_PublicFixtureRegistrationTest
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_LIST_DIR}/cmake/PublicFixtureRegistrationTest.cmake")

function(add_dse_external_media_test name)
    if(DSE_EXTERNAL_FIXTURES_AVAILABLE)
        add_test(NAME "${name}_ExternalMedia" COMMAND "${name}" ${ARGN})
        set_tests_properties("${name}_ExternalMedia" PROPERTIES LABELS external-media)
    endif()
endfunction()

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

include(${CMAKE_SOURCE_DIR}/Player/SessionApplication.cmake)

if(WIN32 AND BUILD_PLAYER)
    include(${CMAKE_SOURCE_DIR}/DesktopSceneEngineIII/cmake/WebView2.cmake)

    function(add_dse_product_test name)
        add_executable(DesktopSceneEngine_${name} Player/${name}.cpp)
        target_include_directories(DesktopSceneEngine_${name}
            PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/Engine/src)
        target_compile_features(DesktopSceneEngine_${name} PRIVATE cxx_std_20)
        target_compile_options(DesktopSceneEngine_${name} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
        target_link_libraries(DesktopSceneEngine_${name} PRIVATE DSE::ProductPresentation)
        deploy_ffmpeg_dlls(DesktopSceneEngine_${name})
    endfunction()

    add_executable(DesktopSceneEngine_ProductWindowHostTest Player/ProductWindowHostTest.cpp)
    target_include_directories(DesktopSceneEngine_ProductWindowHostTest PRIVATE ${CMAKE_SOURCE_DIR}/Player)
    target_link_libraries(DesktopSceneEngine_ProductWindowHostTest PRIVATE DSE::ProductWindowHost user32 dwmapi)
    add_test(NAME DesktopSceneEngine_ProductWindowHostTest COMMAND DesktopSceneEngine_ProductWindowHostTest)
    set_tests_properties(DesktopSceneEngine_ProductWindowHostTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)

    foreach(name LauncherSessionsProjectionTest LauncherBootstrapTest LauncherBridgeContractTest
            LauncherIdentifyTest LauncherLanguageTest LauncherSessionLifecycleTest LauncherCreateOpenTest
            LauncherCreateOpenLifecycleTest LauncherHostLifetimeTest LauncherHardeningLifecycleTest
            SessionBridgeContractTest SessionHostLifetimeTest SessionModelCProductionTest)
        add_dse_product_test(${name})
        add_test(NAME DesktopSceneEngine_${name} COMMAND DesktopSceneEngine_${name})
    endforeach()
    set_tests_properties(DesktopSceneEngine_SessionBridgeContractTest DesktopSceneEngine_SessionHostLifetimeTest
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)
    set_tests_properties(DesktopSceneEngine_LauncherLanguageTest DesktopSceneEngine_SessionModelCProductionTest
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    set_tests_properties(DesktopSceneEngine_LauncherSessionLifecycleTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(DesktopSceneEngine_LauncherCreateOpenTest DesktopSceneEngine_LauncherCreateOpenLifecycleTest
        DesktopSceneEngine_LauncherHostLifetimeTest DesktopSceneEngine_LauncherHardeningLifecycleTest
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
    target_link_libraries(DesktopSceneEngine_LauncherHostLifetimeTest PRIVATE DSEIII::WebView2Loader)
    target_link_libraries(DesktopSceneEngine_SessionHostLifetimeTest PRIVATE DSEIII::WebView2Loader)

    # The frontend language check launches this real host against isolated preferences.
    add_dse_product_test(LauncherLanguageHostFixture)
    foreach(name LauncherLanguageTest LauncherLanguageHostFixture LauncherSessionLifecycleTest LauncherCreateOpenTest
            LauncherCreateOpenLifecycleTest LauncherHostLifetimeTest LauncherHardeningLifecycleTest SessionModelCProductionTest)
        add_dependencies(DesktopSceneEngine_${name} DesktopSceneEngine_QtPlayer)
    endforeach()

    add_dse_product_test(ApplicationCoordinatorTest)
    foreach(case ui-lifetime protocol-lifetime bootstrap-timeout unavailable-join pending-teardown)
        add_test(NAME DesktopSceneEngine_Coordinator_${case} COMMAND DesktopSceneEngine_ApplicationCoordinatorTest ${case})
        set_tests_properties(DesktopSceneEngine_Coordinator_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()
endif()


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
    ${CMAKE_SOURCE_DIR}/Engine/src
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


add_test(NAME DesktopSceneEngine_ActiveProjectionRebuildTest_FixtureIndependent
    COMMAND DesktopSceneEngine_ActiveProjectionRebuildTest --fixture-independent)
add_dse_external_media_test(DesktopSceneEngine_ActiveProjectionRebuildTest)


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


add_executable(
    DesktopSceneEngine_EngineSceneNavigationTest
)


target_sources(
    DesktopSceneEngine_EngineSceneNavigationTest
    PRIVATE

    Core/EngineSceneNavigationTest.cpp
)


target_include_directories(
    DesktopSceneEngine_EngineSceneNavigationTest
    PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src
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


add_test(NAME DesktopSceneEngine_EngineSceneNavigationTest_FixtureIndependent
    COMMAND DesktopSceneEngine_EngineSceneNavigationTest --fixture-independent)
add_dse_external_media_test(DesktopSceneEngine_EngineSceneNavigationTest)


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


add_test(NAME DesktopSceneEngine_EngineSeekTest_Generated
    COMMAND DesktopSceneEngine_EngineSeekTest --generated)
add_dse_external_media_test(DesktopSceneEngine_EngineSeekTest)


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


add_test(NAME DesktopSceneEngine_VideoResourceTest_MissingPath
    COMMAND DesktopSceneEngine_VideoResourceTest)
add_dse_external_media_test(DesktopSceneEngine_VideoResourceTest "${DSE_EXTERNAL_FIRST_VIDEO}")


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
    "${CMAKE_CURRENT_SOURCE_DIR}/TestData/Audio/SequentialAudioSourceCompressed.mp3"
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


add_dse_external_media_test(DesktopSceneEngine_PreviewVideoDecoderTest)
endif()


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


add_test(NAME DesktopSceneEngine_TimelinePreviewProviderTest_Generated
    COMMAND DesktopSceneEngine_TimelinePreviewProviderTest --generated)
add_dse_external_media_test(DesktopSceneEngine_TimelinePreviewProviderTest)


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


add_test(NAME DesktopSceneEngine_TimelinePreviewWorkerTest_Generated
    COMMAND DesktopSceneEngine_TimelinePreviewWorkerTest --generated)
add_dse_external_media_test(DesktopSceneEngine_TimelinePreviewWorkerTest)


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


add_dse_external_media_test(DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest)
endif()


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


if(TARGET DesktopSceneEngine_QtPlayer_lrelease)
    add_dependencies(DesktopSceneEngine_ApplicationPreferencesTest DesktopSceneEngine_QtPlayer_lrelease)
    set(preferences_test_catalog "${CMAKE_BINARY_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.qm")
else()
    find_package(Qt6 CONFIG REQUIRED COMPONENTS LinguistTools)
    qt_add_lrelease(DesktopSceneEngine_ApplicationPreferencesTest
        TS_FILES "${CMAKE_SOURCE_DIR}/Player/i18n/DesktopSceneEngine_zh_CN.ts"
        QM_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/i18n"
        QM_FILES_OUTPUT_VARIABLE preferences_test_catalog)
endif()


add_custom_command(
    TARGET DesktopSceneEngine_ApplicationPreferencesTest
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DesktopSceneEngine_ApplicationPreferencesTest>/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${preferences_test_catalog}"
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

    if(TARGET DesktopSceneEngine_QtPlayer)
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
    endif()
endif()

if(WIN32 AND BUILD_PLAYER)
    # The focused test includes the implementation to inspect the actual card
    # rasterizer and HWND; do not also compile that .cpp into this target.
    add_executable(DesktopSceneEngine_NativeTimelineHoverWindowTest Player/NativeTimelineHoverWindowTest.cpp)
    target_include_directories(DesktopSceneEngine_NativeTimelineHoverWindowTest PRIVATE ${CMAKE_SOURCE_DIR}/Player)
    target_compile_features(DesktopSceneEngine_NativeTimelineHoverWindowTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_NativeTimelineHoverWindowTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_NativeTimelineHoverWindowTest PRIVATE Qt6::Widgets user32 gdi32)
    add_test(NAME DesktopSceneEngine_NativeTimelineHoverWindowTest COMMAND DesktopSceneEngine_NativeTimelineHoverWindowTest)
    set_tests_properties(DesktopSceneEngine_NativeTimelineHoverWindowTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)
endif()

if(WIN32)
    add_executable(DesktopSceneEngine_EnginePlaybackEofTest EnginePlaybackEofTest.cpp)
    target_include_directories(DesktopSceneEngine_EnginePlaybackEofTest PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_EnginePlaybackEofTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_EnginePlaybackEofTest PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_EnginePlaybackEofTest PRIVATE DesktopSceneEngine::Engine)
    deploy_ffmpeg_dlls(DesktopSceneEngine_EnginePlaybackEofTest)
    add_test(NAME DesktopSceneEngine_EnginePlaybackEofTest COMMAND DesktopSceneEngine_EnginePlaybackEofTest)
    set_tests_properties(DesktopSceneEngine_EnginePlaybackEofTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)
endif()

if(WIN32 AND BUILD_PLAYER)
    add_executable(DesktopSceneEngine_SessionCompositionCapture Player/SessionCompositionCapture.cpp)
    target_compile_features(DesktopSceneEngine_SessionCompositionCapture PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_SessionCompositionCapture PRIVATE /utf-8)
    target_link_libraries(DesktopSceneEngine_SessionCompositionCapture PRIVATE d3d11 dxgi dwmapi windowsapp ole32 user32)
endif()

if(WIN32 AND BUILD_PLAYER)
    add_executable(DesktopSceneEngine_SessionCompositionInputTest Player/SessionCompositionInputTest.cpp)
    target_include_directories(DesktopSceneEngine_SessionCompositionInputTest PRIVATE ${CMAKE_SOURCE_DIR}/Player)
    target_compile_features(DesktopSceneEngine_SessionCompositionInputTest PRIVATE cxx_std_20)
    target_link_libraries(DesktopSceneEngine_SessionCompositionInputTest PRIVATE
        DSE::ProductPresentation DSEIII::WebView2Loader Qt6::Widgets user32 UIAutomationCore)
    add_test(NAME DesktopSceneEngine_SessionCompositionInputTest COMMAND DesktopSceneEngine_SessionCompositionInputTest)
    set_tests_properties(DesktopSceneEngine_SessionCompositionInputTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)
    add_executable(DesktopSceneEngine_SessionWebViewCompositionTest Player/SessionWebViewCompositionTest.cpp)
    target_include_directories(DesktopSceneEngine_SessionWebViewCompositionTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/Engine/src/Rendering)
    target_compile_features(DesktopSceneEngine_SessionWebViewCompositionTest PRIVATE cxx_std_20)
    target_compile_definitions(DesktopSceneEngine_SessionWebViewCompositionTest PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(DesktopSceneEngine_SessionWebViewCompositionTest PRIVATE DSE::ProductPresentation Qt6::Widgets d3d11 dxgi dcomp user32 UIAutomationCore)
    add_test(NAME DesktopSceneEngine_SessionWebViewCompositionTest COMMAND DesktopSceneEngine_SessionWebViewCompositionTest)
    set_tests_properties(DesktopSceneEngine_SessionWebViewCompositionTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 30)
endif()

if(WIN32)
    add_executable(DesktopSceneEngine_HostedCompositionContentTest
        Rendering/HostedCompositionContentTest.cpp)
    target_include_directories(DesktopSceneEngine_HostedCompositionContentTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src/Rendering)
    target_compile_features(DesktopSceneEngine_HostedCompositionContentTest PRIVATE cxx_std_20)
    target_compile_definitions(DesktopSceneEngine_HostedCompositionContentTest
        PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(DesktopSceneEngine_HostedCompositionContentTest
        PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus> $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_HostedCompositionContentTest
        PRIVATE DesktopSceneEngine::Engine dxgi dcomp)
    deploy_ffmpeg_dlls(DesktopSceneEngine_HostedCompositionContentTest)
    add_test(NAME DesktopSceneEngine_HostedCompositionContentTest
        COMMAND DesktopSceneEngine_HostedCompositionContentTest)
    set_tests_properties(DesktopSceneEngine_HostedCompositionContentTest
        PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
endif()

# CP1 tests exercise the production bounded execution/producer implementations.
if(WIN32)
foreach(cp1_test AudioExecution AudioPcmPipe AudioSourcePreparation)
    add_executable(DesktopSceneEngine_${cp1_test}Test Audio/${cp1_test}Test.cpp)
    target_include_directories(DesktopSceneEngine_${cp1_test}Test PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_${cp1_test}Test PRIVATE cxx_std_20)
    target_link_libraries(DesktopSceneEngine_${cp1_test}Test PRIVATE DesktopSceneEngine::Engine)
    deploy_ffmpeg_dlls(DesktopSceneEngine_${cp1_test}Test)
    if(cp1_test STREQUAL "AudioSourcePreparation")
        add_test(NAME DesktopSceneEngine_${cp1_test}Test COMMAND DesktopSceneEngine_${cp1_test}Test "${CMAKE_CURRENT_SOURCE_DIR}/TestData/Audio")
    else()
        add_test(NAME DesktopSceneEngine_${cp1_test}Test COMMAND DesktopSceneEngine_${cp1_test}Test)
    endif()
    set_tests_properties(DesktopSceneEngine_${cp1_test}Test PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
endforeach()
endif()

if(TARGET DSE::Application)
    add_executable(DesktopSceneEngine_SessionApplicationTest Player/SessionApplicationTest.cpp)
    target_include_directories(DesktopSceneEngine_SessionApplicationTest PRIVATE ${CMAKE_SOURCE_DIR}/Player)
    target_compile_features(DesktopSceneEngine_SessionApplicationTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_SessionApplicationTest PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus> $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_SessionApplicationTest PRIVATE DSE::Application Qt6::Core)
    deploy_ffmpeg_dlls(DesktopSceneEngine_SessionApplicationTest)
    add_test(NAME DesktopSceneEngine_SessionApplicationTest COMMAND DesktopSceneEngine_SessionApplicationTest)
    set_tests_properties(DesktopSceneEngine_SessionApplicationTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)

    add_executable(DesktopSceneEngine_SessionRuntimeOperationTest Player/SessionRuntimeOperationTest.cpp)
    target_include_directories(DesktopSceneEngine_SessionRuntimeOperationTest
        PRIVATE ${CMAKE_SOURCE_DIR}/Player ${CMAKE_SOURCE_DIR}/Engine/src)
    target_compile_features(DesktopSceneEngine_SessionRuntimeOperationTest PRIVATE cxx_std_20)
    target_compile_options(DesktopSceneEngine_SessionRuntimeOperationTest PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus> $<$<CXX_COMPILER_ID:MSVC>:/W4>)
    target_link_libraries(DesktopSceneEngine_SessionRuntimeOperationTest PRIVATE DSE::Application Qt6::Widgets)
    deploy_ffmpeg_dlls(DesktopSceneEngine_SessionRuntimeOperationTest)
    foreach(case control-stall-completion reserved-close authored-rebuild)
        add_test(NAME DesktopSceneEngine_SessionRuntime_${case}
            COMMAND DesktopSceneEngine_SessionRuntimeOperationTest --cp3-${case})
        set_tests_properties(DesktopSceneEngine_SessionRuntime_${case} PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
    endforeach()
    add_test(NAME DesktopSceneEngine_SessionRuntime_bgm-authority
        COMMAND DesktopSceneEngine_SessionRuntimeOperationTest --bgm-authority)
    set_tests_properties(DesktopSceneEngine_SessionRuntime_bgm-authority PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
endif()
