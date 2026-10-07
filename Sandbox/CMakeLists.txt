add_executable(
    Sandbox
)


target_sources(
    Sandbox
    PRIVATE

    Application.cpp
    src/main.cpp
)


target_include_directories(
    Sandbox
    PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_link_libraries(
    Sandbox
    PRIVATE

    DesktopSceneEngine::Engine
)


if(WIN32)

    deploy_ffmpeg_dlls(Sandbox)

endif()
