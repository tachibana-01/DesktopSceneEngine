cmake_minimum_required(VERSION 3.25)

project(
    DesktopSceneEngine
    VERSION 0.1.0
    LANGUAGES CXX
)


# ==============================
# Generate compile_commands.json
# ==============================

set(
    CMAKE_EXPORT_COMPILE_COMMANDS
    ON
)


# ==============================
# Build Options
# ==============================

option(
    BUILD_EDITOR
    "Build Qt Editor"
    OFF
)

option(
    BUILD_PLAYER
    "Build the Qt6 Widgets Player"
    OFF
)

option(
    BUILD_TESTING
    "Build tests"
    OFF
)

include(CTest)


# ==============================
# Output Directory
# ==============================

set(
    CMAKE_RUNTIME_OUTPUT_DIRECTORY
    ${CMAKE_BINARY_DIR}/bin
)

set(
    CMAKE_LIBRARY_OUTPUT_DIRECTORY
    ${CMAKE_BINARY_DIR}/lib
)

set(
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    ${CMAKE_BINARY_DIR}/lib
)


# ==============================
# Sub Projects
# ==============================

add_subdirectory(Engine)

if(WIN32)

    get_filename_component(
        FFMPEG_DEBUG_LIB_DIR
        "${FFMPEG_libavcodec_LIBRARY_DEBUG}"
        DIRECTORY
    )

    get_filename_component(
        FFMPEG_RELEASE_LIB_DIR
        "${FFMPEG_libavcodec_LIBRARY_RELEASE}"
        DIRECTORY
    )

    find_file(
        FFMPEG_DEBUG_AVCODEC_DLL
        NAMES avcodec-62.dll
        PATHS "${FFMPEG_DEBUG_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_file(
        FFMPEG_DEBUG_AVFORMAT_DLL
        NAMES avformat-62.dll
        PATHS "${FFMPEG_DEBUG_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_file(
        FFMPEG_DEBUG_AVUTIL_DLL
        NAMES avutil-60.dll
        PATHS "${FFMPEG_DEBUG_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_file(
        FFMPEG_DEBUG_SWSCALE_DLL
        NAMES swscale-9.dll
        PATHS "${FFMPEG_DEBUG_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_file(
        FFMPEG_DEBUG_AVFILTER_DLL
        NAMES avfilter-11.dll
        PATHS "${FFMPEG_DEBUG_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    get_filename_component(
        FFMPEG_DEBUG_SWRESAMPLE_LIB_DIR
        "${FFMPEG_libswresample_LIBRARY_DEBUG}"
        DIRECTORY
    )

    get_filename_component(
        FFMPEG_RELEASE_SWRESAMPLE_LIB_DIR
        "${FFMPEG_libswresample_LIBRARY_RELEASE}"
        DIRECTORY
    )

    find_file(
        FFMPEG_DEBUG_SWRESAMPLE_DLL
        NAMES swresample-6.dll
        PATHS "${FFMPEG_DEBUG_SWRESAMPLE_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_file(
        FFMPEG_RELEASE_SWRESAMPLE_DLL
        NAMES swresample-6.dll
        PATHS "${FFMPEG_RELEASE_SWRESAMPLE_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_file(
        FFMPEG_RELEASE_AVCODEC_DLL
        NAMES avcodec-62.dll
        PATHS "${FFMPEG_RELEASE_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_file(
        FFMPEG_RELEASE_AVFORMAT_DLL
        NAMES avformat-62.dll
        PATHS "${FFMPEG_RELEASE_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_file(
        FFMPEG_RELEASE_AVUTIL_DLL
        NAMES avutil-60.dll
        PATHS "${FFMPEG_RELEASE_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    find_file(
        FFMPEG_RELEASE_SWSCALE_DLL
        NAMES swscale-9.dll
        PATHS "${FFMPEG_RELEASE_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    get_filename_component(
        FFMPEG_RELEASE_AVFILTER_LIB_DIR
        "${FFMPEG_libavfilter_LIBRARY_RELEASE}"
        DIRECTORY
    )

    find_file(
        FFMPEG_RELEASE_AVFILTER_DLL
        NAMES avfilter-11.dll
        PATHS "${FFMPEG_RELEASE_AVFILTER_LIB_DIR}/../bin"
        NO_DEFAULT_PATH
        REQUIRED
    )

    function(deploy_ffmpeg_dlls target)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${FFMPEG_DEBUG_AVCODEC_DLL}>$<$<NOT:$<CONFIG:Debug>>:${FFMPEG_RELEASE_AVCODEC_DLL}>"
                "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${FFMPEG_DEBUG_AVFORMAT_DLL}>$<$<NOT:$<CONFIG:Debug>>:${FFMPEG_RELEASE_AVFORMAT_DLL}>"
                "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${FFMPEG_DEBUG_AVUTIL_DLL}>$<$<NOT:$<CONFIG:Debug>>:${FFMPEG_RELEASE_AVUTIL_DLL}>"
                "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${FFMPEG_DEBUG_SWSCALE_DLL}>$<$<NOT:$<CONFIG:Debug>>:${FFMPEG_RELEASE_SWSCALE_DLL}>"
                "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${FFMPEG_DEBUG_AVFILTER_DLL}>$<$<NOT:$<CONFIG:Debug>>:${FFMPEG_RELEASE_AVFILTER_DLL}>"
                "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${FFMPEG_DEBUG_SWRESAMPLE_DLL}>$<$<NOT:$<CONFIG:Debug>>:${FFMPEG_RELEASE_SWRESAMPLE_DLL}>"
                "$<TARGET_FILE_DIR:${target}>"
        )
    endfunction()

endif()


add_subdirectory(Sandbox)


if(BUILD_PLAYER)

    add_subdirectory(Player)

endif()


if(BUILD_EDITOR)

    add_subdirectory(Editor)

endif()


if(BUILD_TESTING)

    add_subdirectory(tests)

endif()
