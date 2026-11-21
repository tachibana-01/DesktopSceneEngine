find_package(
    Qt6 6.9
    CONFIG
    REQUIRED
    COMPONENTS Widgets Network LinguistTools
)

include("${CMAKE_CURRENT_SOURCE_DIR}/ProductPresentation.cmake")

add_executable(DesktopSceneEngine_QtPlayer WIN32 main.cpp)
target_link_libraries(DesktopSceneEngine_QtPlayer PRIVATE DSE::ProductPresentation)
target_compile_options(DesktopSceneEngine_QtPlayer PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus;/W4>)

find_program(DSE_NPM_EXECUTABLE NAMES npm.cmd REQUIRED)
set(DSE_PRODUCT_FRONTEND "${CMAKE_SOURCE_DIR}/DesktopSceneEngineIII/Frontend")
add_custom_target(DesktopSceneEngine_ProductFrontend
    COMMAND "${DSE_NPM_EXECUTABLE}" run build
    COMMAND ${CMAKE_COMMAND}
        "-DBINARY_ROOT=${CMAKE_BINARY_DIR}"
        "-DPRODUCT_DIRECTORY=$<TARGET_FILE_DIR:DesktopSceneEngine_QtPlayer>"
        "-DFRONTEND_DIST=${DSE_PRODUCT_FRONTEND}/dist"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/StageProductFrontend.cmake"
    WORKING_DIRECTORY "${DSE_PRODUCT_FRONTEND}"
    COMMENT "Build and stage the production WebView2 package"
    VERBATIM)
add_dependencies(DesktopSceneEngine_QtPlayer DesktopSceneEngine_ProductFrontend)


set(
    DESKTOP_SCENE_ENGINE_PLAYER_TRANSLATION_TS

    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/DesktopSceneEngine_zh_CN.ts
)


qt_add_lrelease(
    DesktopSceneEngine_QtPlayer

    LRELEASE_TARGET DesktopSceneEngine_QtPlayer_lrelease
    TS_FILES ${DESKTOP_SCENE_ENGINE_PLAYER_TRANSLATION_TS}
    QM_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/i18n
    QM_FILES_OUTPUT_VARIABLE DESKTOP_SCENE_ENGINE_PLAYER_TRANSLATION_QM
)


add_custom_command(
    TARGET DesktopSceneEngine_QtPlayer
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DesktopSceneEngine_QtPlayer>/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${DESKTOP_SCENE_ENGINE_PLAYER_TRANSLATION_QM}
        $<TARGET_FILE_DIR:DesktopSceneEngine_QtPlayer>/i18n
    VERBATIM
)


if(WIN32)

    target_sources(DesktopSceneEngine_QtPlayer PRIVATE Player.manifest Player.rc)

    qt_add_resources(DesktopSceneEngine_QtPlayer application_icon
        PREFIX "/icons"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/DesktopSceneEngine.ico"
    )

    deploy_ffmpeg_dlls(DesktopSceneEngine_QtPlayer)

    get_filename_component(
        QT6_PREFIX_DIR
        "${Qt6_DIR}/../../../"
        ABSOLUTE
    )

    set(
        QT6_ZH_CN_TRANSLATION
        "${QT6_PREFIX_DIR}/translations/qtbase_zh_CN.qm"
    )

    if(EXISTS "${QT6_ZH_CN_TRANSLATION}")

        add_custom_command(
            TARGET DesktopSceneEngine_QtPlayer
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_FILE_DIR:DesktopSceneEngine_QtPlayer>/i18n
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QT6_ZH_CN_TRANSLATION}"
                $<TARGET_FILE_DIR:DesktopSceneEngine_QtPlayer>/i18n
            VERBATIM
        )

    else()

        message(WARNING "Qt Simplified Chinese catalog was not found: ${QT6_ZH_CN_TRANSLATION}")

    endif()

    find_program(
        QT6_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${QT6_PREFIX_DIR}/bin"
        REQUIRED
    )

    add_custom_command(
        TARGET DesktopSceneEngine_QtPlayer
        POST_BUILD
        COMMAND "${QT6_WINDEPLOYQT_EXECUTABLE}"
            "$<TARGET_FILE:DesktopSceneEngine_QtPlayer>"
            --no-translations
            "$<$<CONFIG:Debug>:--debug>$<$<NOT:$<CONFIG:Debug>>:--release>"
        VERBATIM
    )

endif()
