file(READ "${CMAKE_CURRENT_LIST_DIR}/../dependency-lock.json" dependency_lock)
string(JSON webview2_version GET "${dependency_lock}" webView2 version)
string(JSON webview2_header_hash GET "${dependency_lock}" webView2 headerSha256)
string(JSON webview2_loader_hash GET "${dependency_lock}" webView2 loaderSha256)

set(webview2_root "${CMAKE_CURRENT_LIST_DIR}/../.deps/Microsoft.Web.WebView2.${webview2_version}")
set(webview2_header "${webview2_root}/build/native/include/WebView2.h")
set(webview2_loader "${webview2_root}/build/native/x64/WebView2LoaderStatic.lib")

foreach(artifact IN ITEMS "${webview2_header}" "${webview2_loader}")
    if(NOT EXISTS "${artifact}")
        message(FATAL_ERROR "Missing ${artifact}. Run scripts/Restore-WebView2.ps1 first.")
    endif()
endforeach()

file(SHA256 "${webview2_header}" actual_header_hash)
file(SHA256 "${webview2_loader}" actual_loader_hash)
if(NOT actual_header_hash STREQUAL webview2_header_hash OR
   NOT actual_loader_hash STREQUAL webview2_loader_hash)
    message(FATAL_ERROR "WebView2 SDK artifacts do not match dependency-lock.json.")
endif()

add_library(DSEIII::WebView2Loader STATIC IMPORTED)
set_target_properties(DSEIII::WebView2Loader PROPERTIES
    IMPORTED_LOCATION "${webview2_loader}"
    INTERFACE_INCLUDE_DIRECTORIES "${webview2_root}/build/native/include"
    INTERFACE_COMPILE_DEFINITIONS "UNICODE;_UNICODE;WIN32_LEAN_AND_MEAN;NOMINMAX"
    INTERFACE_LINK_LIBRARIES "version;ole32;uuid"
)
