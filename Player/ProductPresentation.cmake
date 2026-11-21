if(TARGET DSE::ProductPresentation)
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/SessionApplication.cmake")
include("${CMAKE_SOURCE_DIR}/DesktopSceneEngineIII/cmake/WebView2.cmake")

add_library(DseProductWindowHost STATIC)
add_library(DSE::ProductWindowHost ALIAS DseProductWindowHost)
target_sources(DseProductWindowHost PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ProductWindowHost.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProductWindowHost.h")
target_include_directories(DseProductWindowHost PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(DseProductWindowHost PUBLIC cxx_std_20)
target_compile_definitions(DseProductWindowHost PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(DseProductWindowHost PUBLIC Qt6::Widgets PRIVATE user32 dwmapi comctl32)

# The executable and native presentation contract tests use this same implementation.
add_library(DseProductPresentation STATIC)
add_library(DSE::ProductPresentation ALIAS DseProductPresentation)
target_sources(DseProductPresentation PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ApplicationCoordinator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/BgmPeerWindow.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/DisplayPresentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/DisplaySessionBootstrap.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/InstanceActivation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/InstanceAdmission.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/LauncherBootstrap.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/LauncherBridgeContract.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/LauncherIdentify.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/LauncherLanguage.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/LauncherSessionsProjection.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ModelCSessionWindow.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/NativeTimelineHoverWindow.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ProductWebViewChrome.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SessionBridgeContract.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SessionCompositionInput.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SessionGuiRelay.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SessionPresentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SessionReadModel.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SessionWebViewComposition.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/SessionWebViewControl.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/TimelinePreviewPresentation.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/TimelinePreviewProvider.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/TimelinePreviewPreloadPlanner.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/TimelinePreviewWorker.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/WebViewLauncherHost.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/WebViewSessionHost.cpp")
set_target_properties(DseProductPresentation PROPERTIES AUTOMOC ON)
target_include_directories(DseProductPresentation PUBLIC "${CMAKE_CURRENT_LIST_DIR}" PRIVATE "${CMAKE_SOURCE_DIR}/Engine/src")
target_compile_features(DseProductPresentation PUBLIC cxx_std_20)
target_compile_options(DseProductPresentation PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus;/W4>)
target_link_libraries(DseProductPresentation
    PUBLIC DSE::Application DSE::ProductWindowHost Qt6::Widgets Qt6::Network
    PRIVATE DSEIII::WebView2Loader advapi32 d3d11 dxgi dcomp UIAutomationCore user32)
