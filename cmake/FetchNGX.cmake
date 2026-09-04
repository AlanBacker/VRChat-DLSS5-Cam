# Fetches the NVIDIA DLSS SDK (NGX headers, static import library and the DLSS
# super-resolution runtime) from NVIDIA's public GitHub repository at configure
# time. Pinned to a specific commit for reproducible builds.
#
# This software contains source code provided by NVIDIA Corporation.
# The SDK is used under the NVIDIA RTX SDKs LICENSE (see THIRD_PARTY_NOTICES.md).

set(NGX_SDK_COMMIT "a291cc7d2cc642a51566f3dfd5376f635cd1b284" CACHE STRING "NVIDIA/DLSS commit to fetch (v310.7.0)")
set(NGX_SDK_BASE_URL "https://raw.githubusercontent.com/NVIDIA/DLSS/${NGX_SDK_COMMIT}")
set(NGX_SDK_DIR "${CMAKE_BINARY_DIR}/ngx_sdk")
set(NGX_INCLUDE_DIR "${NGX_SDK_DIR}/include")
set(NGX_STATIC_LIB "${NGX_SDK_DIR}/lib/nvsdk_ngx_s.lib")
set(NGX_DLSS_DLL "${NGX_SDK_DIR}/rel/nvngx_dlss.dll")

set(_ngx_headers
  nvsdk_ngx.h nvsdk_ngx_defs.h nvsdk_ngx_params.h nvsdk_ngx_helpers.h
  nvsdk_ngx_defs_dlssd.h nvsdk_ngx_helpers_dlssd.h nvsdk_ngx_params_dlssd.h
  nvsdk_ngx_defs_dlssg.h nvsdk_ngx_helpers_dlssg.h nvsdk_ngx_params_dlssg.h
  nvsdk_ngx_vk.h nvsdk_ngx_defs_vk.h nvsdk_ngx_helpers_vk.h
  nvsdk_ngx_helpers_dlssd_vk.h nvsdk_ngx_helpers_dlssg_vk.h nvsdk_ngx_helpers_dlssd_cuda.h)

function(_ngx_download url dest)
  if(EXISTS "${dest}")
    return()
  endif()
  message(STATUS "Downloading ${url}")
  file(DOWNLOAD "${url}" "${dest}.tmp" STATUS _st TLS_VERIFY ON SHOW_PROGRESS)
  list(GET _st 0 _code)
  if(NOT _code EQUAL 0)
    list(GET _st 1 _msg)
    file(REMOVE "${dest}.tmp")
    message(FATAL_ERROR "Failed to download ${url}: ${_msg}")
  endif()
  file(RENAME "${dest}.tmp" "${dest}")
endfunction()

file(MAKE_DIRECTORY "${NGX_INCLUDE_DIR}" "${NGX_SDK_DIR}/lib" "${NGX_SDK_DIR}/rel")
foreach(_h IN LISTS _ngx_headers)
  _ngx_download("${NGX_SDK_BASE_URL}/include/${_h}" "${NGX_INCLUDE_DIR}/${_h}")
endforeach()
_ngx_download("${NGX_SDK_BASE_URL}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib" "${NGX_STATIC_LIB}")
_ngx_download("${NGX_SDK_BASE_URL}/lib/Windows_x86_64/rel/nvngx_dlss.dll" "${NGX_DLSS_DLL}")
_ngx_download("${NGX_SDK_BASE_URL}/LICENSE.txt" "${NGX_SDK_DIR}/NVIDIA_LICENSE.txt")
message(STATUS "NVIDIA DLSS SDK ready in ${NGX_SDK_DIR}")
