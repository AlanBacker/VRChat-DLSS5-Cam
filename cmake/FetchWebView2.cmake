# Fetches the Microsoft Edge WebView2 SDK from NuGet at configure time (headers and the static loader library).
# The Ask AI panel hosts the documentation's AI Q&A in a WebView2 control; the static loader keeps the program free
# of an extra DLL, and the WebView2 Runtime itself is part of Windows 10/11 (the panel falls back to the browser
# without it). BSD-3-Clause, see THIRD_PARTY_NOTICES.md. Uses _vdc_fetch_nupkg from FetchOnnxRuntime.cmake.

set(WEBVIEW2_VERSION "1.0.4258.31" CACHE STRING "Microsoft.Web.WebView2 NuGet version")
set(WEBVIEW2_SHA256  "56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9" CACHE STRING "SHA-256 of the WebView2 nupkg")

set(WEBVIEW2_PKG_DIR "${ORT_ROOT}/webview2")
set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_PKG_DIR}/build/native/include")
set(WEBVIEW2_STATIC_LIB "${WEBVIEW2_PKG_DIR}/build/native/x64/WebView2LoaderStatic.lib")

_vdc_fetch_nupkg(Microsoft.Web.WebView2 "${WEBVIEW2_VERSION}" "${WEBVIEW2_SHA256}" "${WEBVIEW2_PKG_DIR}"
  "build/native/include/*" "build/native/x64/WebView2LoaderStatic.lib" "LICENSE.txt")

foreach(_f IN ITEMS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h" "${WEBVIEW2_STATIC_LIB}" "${WEBVIEW2_PKG_DIR}/LICENSE.txt")
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "Expected file missing after extraction: ${_f}")
  endif()
endforeach()
configure_file("${WEBVIEW2_PKG_DIR}/LICENSE.txt" "${ORT_LICENSE_DIR}/WebView2-LICENSE.txt" COPYONLY)
message(STATUS "WebView2 SDK ${WEBVIEW2_VERSION} ready in ${WEBVIEW2_PKG_DIR}")
