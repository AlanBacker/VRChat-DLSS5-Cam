# Fetches the FidelityFX API headers and the signed FSR 3.1 runtime (amd_fidelityfx_dx12.dll) of AMD's FidelityFX
# SDK from its public GitHub repository at configure time. Pinned to the v1.1.4 tag and verified by SHA-256.
# MIT License, Copyright (C) 2024 Advanced Micro Devices, Inc. (see THIRD_PARTY_NOTICES.md).
#
# The DLL hosts the FSR context of the FSR host route (src/gfx/FsrHost.cpp), the route the neural pass takes on a
# Radeon card, where DLSS-NR-on-AMD (a separate program) attaches to the FidelityFX API of the process.

set(FFX_SDK_TAG "v1.1.4" CACHE STRING "FidelityFX SDK tag to fetch")
set(FFX_SDK_BASE_URL "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/${FFX_SDK_TAG}")
set(FFX_SDK_DIR "${CMAKE_BINARY_DIR}/ffx_sdk")
set(FFX_INCLUDE_DIR "${FFX_SDK_DIR}/include")
set(FFX_DX12_DLL "${FFX_SDK_DIR}/bin/amd_fidelityfx_dx12.dll")
set(FFX_LICENSE_FILE "${FFX_SDK_DIR}/LICENSE.txt")

function(_ffx_download rel dest sha256)
  if(EXISTS "${dest}")
    file(SHA256 "${dest}" _have)
    if(_have STREQUAL "${sha256}")
      return()
    endif()
    file(REMOVE "${dest}")
  endif()
  message(STATUS "Downloading ${FFX_SDK_BASE_URL}/${rel}")
  file(DOWNLOAD "${FFX_SDK_BASE_URL}/${rel}" "${dest}.tmp" STATUS _st TLS_VERIFY ON SHOW_PROGRESS
       EXPECTED_HASH "SHA256=${sha256}")
  list(GET _st 0 _code)
  if(NOT _code EQUAL 0)
    list(GET _st 1 _msg)
    file(REMOVE "${dest}.tmp")
    message(FATAL_ERROR "Failed to download ${rel}: ${_msg}")
  endif()
  file(RENAME "${dest}.tmp" "${dest}")
endfunction()

file(MAKE_DIRECTORY "${FFX_INCLUDE_DIR}/ffx_api/dx12" "${FFX_SDK_DIR}/bin")
_ffx_download("ffx-api/include/ffx_api/ffx_api.h"           "${FFX_INCLUDE_DIR}/ffx_api/ffx_api.h"           "408b4203545e8299ed188fbb1d9ddfe0932b73ea6e8b14fe10edcf1f45771561")
_ffx_download("ffx-api/include/ffx_api/ffx_api_types.h"     "${FFX_INCLUDE_DIR}/ffx_api/ffx_api_types.h"     "dee2782813d22047c068e0fb27d1347324f8ce79d280a4672848b3b294cb12fe")
_ffx_download("ffx-api/include/ffx_api/ffx_api_loader.h"    "${FFX_INCLUDE_DIR}/ffx_api/ffx_api_loader.h"    "7be51b7bf1e640b84c0c7f22a9db50a6816debce3755b51224e56c89501659c3")
_ffx_download("ffx-api/include/ffx_api/ffx_upscale.h"       "${FFX_INCLUDE_DIR}/ffx_api/ffx_upscale.h"       "1155bcdcea9c10e6a95507bc9422ca062a853ac84250f0b09a637a66836b3ac1")
_ffx_download("ffx-api/include/ffx_api/dx12/ffx_api_dx12.h" "${FFX_INCLUDE_DIR}/ffx_api/dx12/ffx_api_dx12.h" "215804fba316acb81d2c463b6bbce36c53cc63465c15b8c9858554c8111fae24")
_ffx_download("PrebuiltSignedDLL/amd_fidelityfx_dx12.dll"   "${FFX_DX12_DLL}"                                "12a5081257ec95b0b53ad51b4a87fb3c03f97fe0bbb59f9496968f8d50ef93a6")
_ffx_download("LICENSE.txt"                                 "${FFX_LICENSE_FILE}"                            "82cf74fc23885107c7f69249ac3f1dac55898ff68320eb2c753984cae6b2d78a")
