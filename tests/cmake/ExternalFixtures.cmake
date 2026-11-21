# Local media is opt-in. A clean clone never searches for development media.
set(DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR "" CACHE STRING
    "Absolute external image/video fixture root; see tests/README.md")
set(DSE_EXTERNAL_FIXTURES_AVAILABLE FALSE)

if(DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR STREQUAL "")
    message(STATUS "External media tests: UNAVAILABLE (no explicit fixture root); see tests/README.md.")
    return()
endif()

file(TO_CMAKE_PATH "${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
    DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR)
if(NOT IS_ABSOLUTE "${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
   OR NOT IS_DIRECTORY "${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}"
   OR DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR MATCHES "[\";]")
    message(FATAL_ERROR "External fixture configuration: specify an existing absolute root without quotes or semicolons.")
endif()

# Discovery checks only. The registered tests still exercise the real codecs,
# duration, embedded audio, cancellation and playback contracts.
file(GLOB image_candidates LIST_DIRECTORIES FALSE "${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}/Images/*")
file(GLOB video_candidates LIST_DIRECTORIES FALSE "${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}/Videos/*")
file(GLOB_RECURSE png_candidates LIST_DIRECTORIES FALSE "${DESKTOP_SCENE_ENGINE_EXTERNAL_FIXTURES_DIR}/*.png")
set(images "")
set(videos "")
set(pngs "")
foreach(path IN LISTS image_candidates)
    get_filename_component(extension "${path}" LAST_EXT)
    if(extension MATCHES "^\\.(jpg|jpeg|png)$")
        list(APPEND images "${path}")
    endif()
endforeach()
foreach(path IN LISTS video_candidates)
    get_filename_component(extension "${path}" LAST_EXT)
    if(extension STREQUAL ".mp4")
        list(APPEND videos "${path}")
    endif()
endforeach()
foreach(path IN LISTS png_candidates)
    get_filename_component(extension "${path}" LAST_EXT)
    if(extension STREQUAL ".png")
        list(APPEND pngs "${path}")
    endif()
endforeach()
list(LENGTH images image_count)
list(LENGTH videos video_count)
list(LENGTH pngs png_count)
if(image_count LESS 2 OR video_count LESS 2 OR png_count LESS 1)
    message(FATAL_ERROR "External fixture configuration: supply two lowercase-extension Images JPG/JPEG/PNG, two Videos MP4 and one recursive PNG. See tests/README.md.")
endif()

list(GET videos 0 DSE_EXTERNAL_FIRST_VIDEO)
set(DSE_EXTERNAL_FIXTURES_AVAILABLE TRUE)
message(STATUS "External media tests: enabled from the explicit fixture root.")
