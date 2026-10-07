find_package(
    Qt6
    CONFIG
    REQUIRED
    COMPONENTS Widgets Network LinguistTools
)


add_executable(
    DesktopSceneEngine_QtPlayer
    WIN32
)


target_sources(
    DesktopSceneEngine_QtPlayer
    PRIVATE

    MainWindow.h
    MainWindow.cpp
    ApplicationPreferences.h
    ApplicationPreferences.cpp
    DisplaySessionBootstrap.h
    DisplaySessionBootstrap.cpp
    InstanceAdmission.h
    InstanceAdmission.cpp
    InstanceActivation.h
    InstanceActivation.cpp
    LauncherWindow.h
    LauncherWindow.cpp
    ProjectDocumentState.h
    EffectiveVolume.h
    DisplaySelectionState.h
    DisplaySelectionState.cpp
    ActivePlaybackProjection.h
    ActivePlaybackProjection.cpp
    PreviewViewportHost.h
    PreviewViewportHost.cpp
    TimelinePreviewProvider.h
    TimelinePreviewProvider.cpp
    TimelinePreviewWorker.h
    TimelinePreviewWorker.cpp
    TimelinePreviewPreloadPlanner.h
    TimelinePreviewPreloadPlanner.cpp
    TimelinePreviewPresentation.h
    TimelinePreviewPresentation.cpp
    TimelineHoverGeometry.h
    TimelineRepresentativeTimestamp.h
    TimelinePreviewSource.h
    CanvasPresentationGeometry.h
    CanvasChromeFrame.h
    CanvasChromeFrame.cpp
    BackgroundMusicControlAdapter.h
    BackgroundMusicControlAdapter.cpp
    BackgroundMusicWindow.h
    BackgroundMusicWindow.cpp
    WorkspaceModel.cpp
    main.cpp
)


set_target_properties(
    DesktopSceneEngine_QtPlayer
    PROPERTIES

    AUTOMOC ON
)


target_include_directories(
    DesktopSceneEngine_QtPlayer
    PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Engine/src
)


target_compile_features(
    DesktopSceneEngine_QtPlayer
    PRIVATE

    cxx_std_20
)


target_compile_options(
    DesktopSceneEngine_QtPlayer
    PRIVATE

    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)


target_link_libraries(
    DesktopSceneEngine_QtPlayer
    PRIVATE

    DesktopSceneEngine::Engine
    Qt6::Widgets
    Qt6::Network
    advapi32
    user32
)


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
