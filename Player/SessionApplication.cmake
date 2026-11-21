# One implementation shared by Player and the native adapter tests, including
# BUILD_TESTING=ON / BUILD_PLAYER=OFF. Qt Core/Gui must already be available.
if(TARGET DSE::Application)
    return()
endif()

add_library(DesktopSceneEngineApplication STATIC
    "${CMAKE_SOURCE_DIR}/Player/SessionApplication.h"
    "${CMAKE_SOURCE_DIR}/Player/SessionApplication.cpp"
    "${CMAKE_SOURCE_DIR}/Player/SessionControlExecutor.h"
    "${CMAKE_SOURCE_DIR}/Player/SessionControlExecutor.cpp"
    "${CMAKE_SOURCE_DIR}/Player/SessionRuntimeMedia.cpp"
    "${CMAKE_SOURCE_DIR}/Player/SessionDetachedDesktop.cpp"
    "${CMAKE_SOURCE_DIR}/Player/SessionPlatform.h"
    "${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.h"
    "${CMAKE_SOURCE_DIR}/Player/WorkspaceModel.cpp"
    "${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.h"
    "${CMAKE_SOURCE_DIR}/Player/ActivePlaybackProjection.cpp"
    "${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.h"
    "${CMAKE_SOURCE_DIR}/Player/DisplaySelectionState.cpp"
    "${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.h"
    "${CMAKE_SOURCE_DIR}/Player/ApplicationPreferences.cpp"
    "${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.h"
    "${CMAKE_SOURCE_DIR}/Player/BackgroundMusicControlAdapter.cpp"
    "${CMAKE_SOURCE_DIR}/Player/ProjectDocumentState.h"
    "${CMAKE_SOURCE_DIR}/Player/PlaybackSequenceController.h")
add_library(DSE::Application ALIAS DesktopSceneEngineApplication)
target_compile_features(DesktopSceneEngineApplication PUBLIC cxx_std_20)
target_include_directories(DesktopSceneEngineApplication
    PUBLIC "${CMAKE_SOURCE_DIR}/Player"
    PRIVATE "${CMAKE_SOURCE_DIR}/Engine/src")
target_link_libraries(DesktopSceneEngineApplication PUBLIC
    DesktopSceneEngine::Engine Qt6::Core Qt6::Gui)
target_compile_options(DesktopSceneEngineApplication PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>)
