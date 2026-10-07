cmake_minimum_required(VERSION 3.25)

# Exercise the real registration file without Qt, FFmpeg, a compiler or a build
# directory. The marker files test discovery only, never media decoding.
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

    set(failures "")
    foreach(case unconfigured complete invalid incomplete)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DDSE_REGISTRATION_CASE=${case}"
                "-DDSE_REGISTRATION_FIXTURES=${fixtures}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
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
    return()
endif()

# Capture registration and compile-definition semantics; target construction and
# dependency lookup are deliberately not a substitute for a full Windows build.
function(add_executable target)
    set_property(GLOBAL APPEND PROPERTY captured_targets "${target}")
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
function(target_sources)
endfunction()
function(target_include_directories)
endfunction()
function(target_compile_features)
endfunction()
function(target_compile_options)
endfunction()
function(target_link_libraries)
endfunction()
function(set_target_properties)
endfunction()
function(set_tests_properties)
endfunction()
function(add_dependencies)
endfunction()
function(add_custom_command)
endfunction()
function(deploy_ffmpeg_dlls)
endfunction()

get_filename_component(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
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
message(STATUS "${DSE_REGISTRATION_CASE}: registration assertions PASS")
