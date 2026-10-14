cmake_minimum_required(VERSION 3.25)

# Exercise the real registration file without Qt, FFmpeg or compilation.
# A temporary LANGUAGES NONE configure supplies native TARGET/directory queries;
# interface placeholders never compile the captured executable sources.
# Marker files test discovery only, never media decoding.
if(NOT DEFINED DSE_REGISTRATION_CASE)
    string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef nonce)
    set(fixtures "${CMAKE_CURRENT_BINARY_DIR}/registration-fixtures-${nonce}")
    if(EXISTS "${fixtures}")
        message(FATAL_ERROR "Refusing to reuse a fixture-test directory")
    endif()
    file(MAKE_DIRECTORY "${fixtures}/Images" "${fixtures}/Videos")
    foreach(name first.frame.png second.frame.jpg)
        file(WRITE "${fixtures}/Images/${name}" "registration marker only\n")
    endforeach()
    foreach(name first.clip.mp4 second.clip.mp4)
        file(WRITE "${fixtures}/Videos/${name}" "registration marker only\n")
    endforeach()

    set(harness "${fixtures}/registration")
    file(MAKE_DIRECTORY "${harness}")
    file(WRITE "${harness}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.25)\n"
        "project(PublicRegistrationCapture LANGUAGES NONE)\n"
        "include([==[${CMAKE_CURRENT_LIST_FILE}]==])\n")

    set(failures "")
    foreach(case unconfigured complete invalid incomplete)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                -S "${harness}" -B "${fixtures}/build-${case}"
                "-DDSE_REGISTRATION_CASE=${case}"
                "-DDSE_REGISTRATION_FIXTURES=${fixtures}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE error)
        if(case STREQUAL "invalid" OR case STREQUAL "incomplete")
            if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "External fixture configuration:")
                string(APPEND failures "${case}: expected an explicit fixture configuration failure\n${output}${error}\n")
            endif()
        elseif(NOT result EQUAL 0)
            string(APPEND failures "${case}: ${output}${error}\n")
        endif()
    endforeach()

    # This is the exact nonce directory created above, not a caller-provided root.
    cmake_path(IS_PREFIX CMAKE_CURRENT_BINARY_DIR "${fixtures}" NORMALIZE owned)
    if(NOT owned OR fixtures STREQUAL CMAKE_CURRENT_BINARY_DIR)
        message(FATAL_ERROR "Fixture-test cleanup boundary mismatch")
    endif()
    file(REMOVE_RECURSE "${fixtures}")
    if(EXISTS "${fixtures}")
        message(FATAL_ERROR "Fixture-test cleanup failed")
    endif()
    if(failures)
        message(FATAL_ERROR "${failures}")
    endif()
    message(STATUS "Public fixture registration: all four cases PASS")
    message(STATUS "Tray capture: eight targets, source inheritance, linkage, definitions and CTest assertions PASS in both configured fixture states")
    return()
endif()

# Capture registration and compile-definition semantics; target construction and
# dependency lookup are deliberately not a substitute for a full Windows build.
function(add_executable target)
    add_library("${target}" INTERFACE)
    set_property(GLOBAL APPEND PROPERTY captured_targets "${target}")
    set(sources ${ARGN})
    list(REMOVE_ITEM sources WIN32 MACOSX_BUNDLE EXCLUDE_FROM_ALL)
    set_property(GLOBAL PROPERTY "sources_${target}" "${sources}")
endfunction()
function(target_sources target)
    set(sources ${ARGN})
    list(REMOVE_ITEM sources PRIVATE PUBLIC INTERFACE)
    set_property(GLOBAL APPEND PROPERTY "sources_${target}" "${sources}")
endfunction()
function(get_target_property output target property)
    if(NOT TARGET "${target}" OR NOT property STREQUAL "SOURCES")
        message(FATAL_ERROR "Unsupported captured target property: ${target}/${property}")
    endif()
    get_property(sources GLOBAL PROPERTY "sources_${target}")
    set("${output}" "${sources}" PARENT_SCOPE)
endfunction()
function(add_test)
    cmake_parse_arguments(test "" "NAME" "COMMAND" ${ARGV})
    set_property(GLOBAL APPEND PROPERTY captured_tests "${test_NAME}")
    set_property(GLOBAL PROPERTY "command_${test_NAME}" "${test_COMMAND}")
endfunction()
function(target_compile_definitions target)
    set_property(GLOBAL APPEND PROPERTY "definitions_${target}" "${ARGN}")
endfunction()
function(find_package)
endfunction()
function(target_include_directories)
endfunction()
function(target_compile_features target)
    set_property(GLOBAL APPEND PROPERTY "features_${target}" "${ARGN}")
endfunction()
function(target_compile_options)
endfunction()
function(target_link_libraries target)
    set_property(GLOBAL APPEND PROPERTY "links_${target}" "${ARGN}")
endfunction()
function(set_target_properties)
endfunction()
function(set_tests_properties)
    set(names "")
    set(properties "")
    set(in_properties FALSE)
    foreach(argument IN LISTS ARGV)
        if(argument STREQUAL "PROPERTIES")
            set(in_properties TRUE)
        elseif(in_properties)
            list(APPEND properties "${argument}")
        else()
            list(APPEND names "${argument}")
        endif()
    endforeach()
    foreach(name IN LISTS names)
        set_property(GLOBAL APPEND PROPERTY "properties_${name}" "${properties}")
    endforeach()
endfunction()
function(add_dependencies target)
    set_property(GLOBAL APPEND PROPERTY "dependencies_${target}" "${ARGN}")
endfunction()
function(add_custom_command)
endfunction()
function(deploy_ffmpeg_dlls)
endfunction()
function(qt_add_resources target)
    set_property(GLOBAL APPEND PROPERTY "resources_${target}" "${ARGN}")
endfunction()

get_filename_component(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
# Names from the parent build make conditional consumers execute, not disappear.
add_library(DesktopSceneEngine_QtPlayer INTERFACE)
add_library(DesktopSceneEngine_QtPlayer_lrelease INTERFACE)
set(WIN32 TRUE)
set(DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR "" CACHE PATH "" FORCE)
if(DSE_REGISTRATION_CASE STREQUAL "complete")
    set(DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR "${DSE_REGISTRATION_FIXTURES}" CACHE PATH "" FORCE)
elseif(DSE_REGISTRATION_CASE STREQUAL "invalid")
    set(DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR "${DSE_REGISTRATION_FIXTURES}/missing" CACHE PATH "" FORCE)
elseif(DSE_REGISTRATION_CASE STREQUAL "incomplete")
    set(DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR "${CMAKE_SOURCE_DIR}" CACHE PATH "" FORCE)
elseif(NOT DSE_REGISTRATION_CASE STREQUAL "unconfigured")
    message(FATAL_ERROR "Unknown registration case")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt")
get_property(targets GLOBAL PROPERTY captured_targets)
get_property(tests GLOBAL PROPERTY captured_tests)
set(external_tests
    DesktopSceneEngine_ActiveProjectionRebuildTest
    DesktopSceneEngine_EngineSceneNavigationTest
    DesktopSceneEngine_EngineSeekTest
    DesktopSceneEngine_PreviewVideoDecoderTest
    DesktopSceneEngine_TimelinePreviewProviderTest
    DesktopSceneEngine_TimelinePreviewWorkerTest
    DesktopSceneEngine_RepresentativeFrameStoreIntegrationTest
    DesktopSceneEngine_MainWindowBackgroundMusicForwardingTest
    DesktopSceneEngine_MainWindowPlaybackSequenceTest)
foreach(target IN LISTS external_tests)
    if(DSE_REGISTRATION_CASE STREQUAL "complete")
        if(NOT target IN_LIST targets OR NOT target IN_LIST tests)
            message(FATAL_ERROR "External fixture test missing after explicit opt-in: ${target}")
        endif()
        get_property(definitions GLOBAL PROPERTY "definitions_${target}")
        if(NOT "DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR=\"${DSE_REGISTRATION_FIXTURES}\"" IN_LIST definitions)
            message(FATAL_ERROR "External fixture root not forwarded to ${target}")
        endif()
    elseif(target IN_LIST targets OR target IN_LIST tests)
        message(FATAL_ERROR "External fixture test requires explicit opt-in: ${target}")
    endif()
endforeach()
foreach(target DesktopSceneEngine_AudioResourceTest DesktopSceneEngine_RepresentativeFrameStoreTest)
    if(NOT target IN_LIST targets OR NOT target IN_LIST tests)
        message(FATAL_ERROR "Portable test was lost: ${target}")
    endif()
endforeach()
get_property(audio_command GLOBAL PROPERTY command_DesktopSceneEngine_AudioResourceTest)
set(expected_audio "${CMAKE_SOURCE_DIR}/tests/TestData/Audio/SequentialAudioSourceCompressed.mp3")
if(NOT audio_command STREQUAL "DesktopSceneEngine_AudioResourceTest;${expected_audio}" OR NOT EXISTS "${expected_audio}")
    message(FATAL_ERROR "AudioResourceTest must exercise the tracked synthetic MP3")
endif()
get_property(store_definitions GLOBAL PROPERTY definitions_DesktopSceneEngine_RepresentativeFrameStoreTest)
if("${store_definitions}" MATCHES "DESKTOP_SCENE_ENGINE_TEST_RESOURCES_DIR")
    message(FATAL_ERROR "RepresentativeFrameStoreTest retains an unused external-fixture definition")
endif()

# Validate the registration file's actual arguments/state. Native directory
# queries above execute the final shared-presentation pass over every fixture.
set(tray_targets
    DesktopSceneEngine_ApplicationCoordinatorTest
    DesktopSceneEngine_InstanceProductionProcessTest
    DesktopSceneEngine_InstanceSessionLifecycleTest
    DesktopSceneEngine_MainWindowDesktopPresentationTest
    DesktopSceneEngine_TrayRolePresentationTest
    DesktopSceneEngine_TrayControlPresentationTest
    DesktopSceneEngine_TrayCloseAllTest
    DesktopSceneEngine_TrayRecoveryTest)
get_property(native_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS tray_targets)
    if(NOT target IN_LIST targets OR NOT target IN_LIST native_targets)
        message(FATAL_ERROR "Tray executable registration missing: ${target}")
    endif()
    string(REGEX REPLACE "^DesktopSceneEngine_" "" basename "${target}")
    get_property(sources GLOBAL PROPERTY "sources_${target}")
    if(NOT "Player/${basename}.cpp" IN_LIST sources)
        message(FATAL_ERROR "Tray executable source missing: ${target}")
    endif()
    get_property(features GLOBAL PROPERTY "features_${target}")
    if(NOT "cxx_std_20" IN_LIST features)
        message(FATAL_ERROR "Tray C++20 requirement missing: ${target}")
    endif()
    get_property(definitions GLOBAL PROPERTY "definitions_${target}")
    if("${definitions}" MATCHES "TEST_RESOURCES|EXTERNAL_FIXTURES")
        message(FATAL_ERROR "Tray test acquired an external media dependency: ${target}")
    endif()
    get_property(links GLOBAL PROPERTY "links_${target}")
    foreach(required DesktopSceneEngine::Engine user32 advapi32)
        if(NOT required IN_LIST links)
            message(FATAL_ERROR "Tray linkage missing: ${target}/${required}")
        endif()
    endforeach()
    if(target STREQUAL "DesktopSceneEngine_InstanceProductionProcessTest")
        set(required_qt Qt6::Core)
    else()
        set(required_qt Qt6::Widgets Qt6::Network)
    endif()
    foreach(required IN LISTS required_qt)
        if(NOT required IN_LIST links)
            message(FATAL_ERROR "Tray Qt linkage missing: ${target}/${required}")
        endif()
    endforeach()
endforeach()

function(require_source_clone target parent previous replacement)
    get_property(expected GLOBAL PROPERTY "sources_${parent}")
    list(REMOVE_ITEM expected "${previous}")
    list(PREPEND expected "${replacement}")
    get_property(actual GLOBAL PROPERTY "sources_${target}")
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Tray source inheritance mismatch: ${target} from ${parent}")
    endif()
endfunction()
require_source_clone(DesktopSceneEngine_TrayRolePresentationTest
    DesktopSceneEngine_InstanceSessionLifecycleTest
    Player/InstanceSessionLifecycleTest.cpp Player/TrayRolePresentationTest.cpp)
require_source_clone(DesktopSceneEngine_TrayControlPresentationTest
    DesktopSceneEngine_InstanceSessionLifecycleTest
    Player/InstanceSessionLifecycleTest.cpp Player/TrayControlPresentationTest.cpp)
require_source_clone(DesktopSceneEngine_TrayCloseAllTest
    DesktopSceneEngine_MainWindowDesktopPresentationTest
    Player/MainWindowDesktopPresentationTest.cpp Player/TrayCloseAllTest.cpp)
require_source_clone(DesktopSceneEngine_TrayRecoveryTest
    DesktopSceneEngine_TrayCloseAllTest
    Player/TrayCloseAllTest.cpp Player/TrayRecoveryTest.cpp)

foreach(target IN LISTS targets)
    get_property(sources GLOBAL PROPERTY "sources_${target}")
    if("${CMAKE_SOURCE_DIR}/Player/MainWindow.cpp" IN_LIST sources
       OR "${CMAKE_SOURCE_DIR}/Player/LauncherWindow.cpp" IN_LIST sources)
        foreach(required SessionPresentation.cpp InstanceActivation.cpp)
            if(NOT "${CMAKE_SOURCE_DIR}/Player/${required}" IN_LIST sources)
                message(FATAL_ERROR "Shared presentation source missing: ${target}/${required}")
            endif()
        endforeach()
        if("${CMAKE_SOURCE_DIR}/Player/LauncherWindow.cpp" IN_LIST sources
           AND NOT "${CMAKE_SOURCE_DIR}/Player/DisplayPresentation.cpp" IN_LIST sources)
            message(FATAL_ERROR "Shared display-label source missing: ${target}")
        endif()
    endif()
endforeach()
foreach(target DesktopSceneEngine_InstanceProductionProcessTest
               DesktopSceneEngine_InstanceSessionLifecycleTest)
    get_property(definitions GLOBAL PROPERTY "definitions_${target}")
    get_property(dependencies GLOBAL PROPERTY "dependencies_${target}")
    if(NOT "P0_PLAYER_EXECUTABLE=\"$<TARGET_FILE:DesktopSceneEngine_QtPlayer>\"" IN_LIST definitions
       OR NOT "DesktopSceneEngine_QtPlayer" IN_LIST dependencies)
        message(FATAL_ERROR "Production Player route missing: ${target}")
    endif()
endforeach()
foreach(target DesktopSceneEngine_TrayRolePresentationTest
               DesktopSceneEngine_TrayCloseAllTest
               DesktopSceneEngine_MainWindowDesktopPresentationTest)
    get_property(dependencies GLOBAL PROPERTY "dependencies_${target}")
    if(NOT "DesktopSceneEngine_QtPlayer_lrelease" IN_LIST dependencies)
        message(FATAL_ERROR "Tray translation dependency missing: ${target}")
    endif()
endforeach()
get_property(recovery_dependencies GLOBAL PROPERTY dependencies_DesktopSceneEngine_TrayRecoveryTest)
get_property(recovery_resources GLOBAL PROPERTY resources_DesktopSceneEngine_TrayRecoveryTest)
get_property(recovery_links GLOBAL PROPERTY links_DesktopSceneEngine_TrayRecoveryTest)
if(NOT "DesktopSceneEngine_QtPlayer" IN_LIST recovery_dependencies
   OR NOT "ole32" IN_LIST recovery_links OR NOT "oleaut32" IN_LIST recovery_links
   OR NOT "${CMAKE_SOURCE_DIR}/Player/resources/DesktopSceneEngine.ico" IN_LIST recovery_resources)
    message(FATAL_ERROR "Recovery Player/resource/COM registration missing")
endif()

function(require_cases target prefix argument_prefix)
    foreach(case IN LISTS ARGN)
        set(name "DesktopSceneEngine_${prefix}_${case}")
        get_property(command GLOBAL PROPERTY "command_${name}")
        get_property(properties GLOBAL PROPERTY "properties_${name}")
        if(NOT command STREQUAL "${target};${argument_prefix}${case}"
           OR NOT "RUN_SERIAL" IN_LIST properties OR NOT "TRUE" IN_LIST properties)
            message(FATAL_ERROR "Tray CTest registration mismatch: ${name}")
        endif()
    endforeach()
endfunction()
require_cases(DesktopSceneEngine_ApplicationCoordinatorTest Coordinator ""
    ui-lifetime protocol-lifetime bootstrap-timeout unavailable-join pending-teardown)
require_cases(DesktopSceneEngine_InstanceProductionProcessTest InstanceProduction ""
    launcher session launcher-race session-race different restart crash launcher-crash
    launcher-crash-bootstrap launcher-crash-unobserved-teardown normal-close
    coordinator-lifetime coordinator-direct session-visibility hidden-close-exit)
require_cases(DesktopSceneEngine_InstanceSessionLifecycleTest SessionPresentation ""
    tray-core tray-state tray-loss tray-expiry tray-epoch tray-stale tray-maximized
    tray-playing tray-modal tray-native-modal tray-unqualified tray-unavailable
    tray-retire-control tray-pending tray-ending tray-delayed-batch
    tray-expired-permit tray-backpressure)
require_cases(DesktopSceneEngine_TrayRolePresentationTest TrayRoles ""
    menu launcher all lifecycle ordering modal qualification snapshot terminal-loss
    replacement overlap unavailable nonactivation native-modal-all application-modal-all drain-join)
require_cases(DesktopSceneEngine_MainWindowDesktopPresentationTest TrayDeployment "--tray-deployment-"
    menu visible hidden stale disconnected minimized failure disconnect-flight revision
    detach sequential showall loss peer rapid busy translation unavailable unavailable-flight)
require_cases(DesktopSceneEngine_MainWindowDesktopPresentationTest TrayClose "--tray-close-"
    menu launcher-minimized launcher-hidden launcher-settings clean minimized-clean hidden-clean
    visible-cancel visible-save visible-discard hidden-save hidden-discard hidden-cancel
    save-as-cancel save-failure modal bgm-modal native-modal bgm-native-modal query query-old
    control-loss show-pending showall-pending duplicate pending-wait uncertain deploy-race
    peer-last peer-replacement peer-late peer-pending-death)
require_cases(DesktopSceneEngine_TrayCloseAllTest TrayCloseAll ""
    action order cancel later replacement busy pending uncertain all-abort save-as save-failure
    bgm-busy preexisting-close preexisting-request unavailable launcher-recreate pending-death
    ordinary-overlap old-result full-accept launcher-only discard save late-drain hidden translation)
require_cases(DesktopSceneEngine_TrayRecoveryTest TrayRecovery ""
    death concurrent rejoin fencing relaunch shell shell-pending disconnect maximized-loss
    minimized-max-loss failed disconnect-death pending-death batch-death production-recovery
    gate-diagnostics prior-key-terminal prior-key-pending prior-key-fencing)
foreach(scale 1 1.5 2)
    get_property(command GLOBAL PROPERTY "command_DesktopSceneEngine_TrayControl_dpi-${scale}")
    get_property(properties GLOBAL PROPERTY "properties_DesktopSceneEngine_TrayControl_dpi-${scale}")
    if(NOT command STREQUAL "DesktopSceneEngine_TrayControlPresentationTest"
       OR NOT "QT_SCALE_FACTOR=${scale}" IN_LIST properties)
        message(FATAL_ERROR "Tray DPI registration missing: ${scale}")
    endif()
endforeach()
get_property(deployed_command GLOBAL PROPERTY command_DesktopSceneEngine_SessionPresentation_tray-deployed)
if(NOT deployed_command STREQUAL "DesktopSceneEngine_MainWindowDesktopPresentationTest;--tray-deployed")
    message(FATAL_ERROR "Deployed Session registration missing")
endif()
message(STATUS "${DSE_REGISTRATION_CASE}: Tray target/source/link/definition/CTest assertions PASS")
message(STATUS "${DSE_REGISTRATION_CASE}: registration assertions PASS")
