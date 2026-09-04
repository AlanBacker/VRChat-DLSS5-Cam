# Fetches ONNX Runtime (DirectML build) and Microsoft DirectML from NuGet, and the
# Depth Anything V2 Small model (ONNX, fp16 weights) from Hugging Face, at
# configure time. Pinned versions and SHA-256 hashes keep the build reproducible.
#
#   ONNX Runtime  - MIT license            (build/native/include, runtimes/win-x64/native)
#   DirectML      - Microsoft DirectML EULA (bin/x64-win/DirectML.dll)
#   Depth Anything V2 Small - Apache-2.0    (onnx-community/depth-anything-v2-small)
#
# The depth model is used by the frame-guidance stage (estimated depth for
# DLSS 5 neural rendering); the application falls back to zero depth when the
# files are missing at run time. See THIRD_PARTY_NOTICES.md.

set(ORT_VERSION "1.24.4" CACHE STRING "Microsoft.ML.OnnxRuntime.DirectML NuGet version")
set(ORT_SHA256  "57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681" CACHE STRING "SHA-256 of the ONNX Runtime nupkg")
set(DML_VERSION "1.15.4" CACHE STRING "Microsoft.AI.DirectML NuGet version")
set(DML_SHA256  "4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9" CACHE STRING "SHA-256 of the DirectML nupkg")
set(DEPTH_MODEL_URL "https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/main/onnx/model_fp16.onnx"
    CACHE STRING "Depth Anything V2 Small ONNX model URL")
set(DEPTH_MODEL_SHA256 "2df6223f206b5164e21f664ace61dabeb9bb6a49b8b5a3e00510b4807d0f5b04" CACHE STRING "SHA-256 of the depth model")
option(VDC_FETCH_DEPTH_MODEL "Download the Depth Anything V2 Small model at configure time" ON)

set(ORT_ROOT "${CMAKE_BINARY_DIR}/onnxruntime")
set(ORT_PKG_DIR "${ORT_ROOT}/ort")
set(DML_PKG_DIR "${ORT_ROOT}/dml")
set(ORT_INCLUDE_DIR "${ORT_PKG_DIR}/build/native/include")
set(ORT_DLL "${ORT_PKG_DIR}/runtimes/win-x64/native/onnxruntime.dll")
set(ORT_PROVIDERS_SHARED_DLL "${ORT_PKG_DIR}/runtimes/win-x64/native/onnxruntime_providers_shared.dll")
set(DML_INCLUDE_DIR "${DML_PKG_DIR}/include")
set(DML_DLL "${DML_PKG_DIR}/bin/x64-win/DirectML.dll")
set(DEPTH_MODEL_DIR "${CMAKE_BINARY_DIR}/models")
set(DEPTH_MODEL_FILE "${DEPTH_MODEL_DIR}/depth_anything_v2_small_fp16.onnx")
set(ORT_LICENSE_DIR "${ORT_ROOT}/licenses")

function(_vdc_download url dest sha256)
  if(EXISTS "${dest}")
    file(SHA256 "${dest}" _have)
    if(_have STREQUAL "${sha256}")
      return()
    endif()
    message(STATUS "Hash mismatch for ${dest}, downloading again")
    file(REMOVE "${dest}")
  endif()
  message(STATUS "Downloading ${url}")
  file(DOWNLOAD "${url}" "${dest}.tmp" STATUS _st TLS_VERIFY ON SHOW_PROGRESS EXPECTED_HASH "SHA256=${sha256}")
  list(GET _st 0 _code)
  if(NOT _code EQUAL 0)
    list(GET _st 1 _msg)
    file(REMOVE "${dest}.tmp")
    message(FATAL_ERROR "Failed to download ${url}: ${_msg}")
  endif()
  file(RENAME "${dest}.tmp" "${dest}")
endfunction()

# NuGet packages are plain zip archives.
function(_vdc_fetch_nupkg id version sha256 dest_dir)
  set(_pkg "${ORT_ROOT}/${id}.${version}.nupkg")
  _vdc_download("https://www.nuget.org/api/v2/package/${id}/${version}" "${_pkg}" "${sha256}")
  if(NOT EXISTS "${dest_dir}/.extracted-${version}")
    message(STATUS "Extracting ${id} ${version}")
    file(REMOVE_RECURSE "${dest_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_pkg}" DESTINATION "${dest_dir}" PATTERNS ${ARGN})
    file(WRITE "${dest_dir}/.extracted-${version}" "")
  endif()
endfunction()

file(MAKE_DIRECTORY "${ORT_ROOT}" "${ORT_LICENSE_DIR}" "${DEPTH_MODEL_DIR}")

_vdc_fetch_nupkg(Microsoft.ML.OnnxRuntime.DirectML "${ORT_VERSION}" "${ORT_SHA256}" "${ORT_PKG_DIR}"
  "build/native/include/*" "runtimes/win-x64/native/*" "LICENSE" "ThirdPartyNotices.txt")
_vdc_fetch_nupkg(Microsoft.AI.DirectML "${DML_VERSION}" "${DML_SHA256}" "${DML_PKG_DIR}"
  "bin/x64-win/DirectML.dll" "include/*" "LICENSE.txt" "LICENSE-CODE.txt" "ThirdPartyNotices.txt")

foreach(_f IN ITEMS "${ORT_INCLUDE_DIR}/onnxruntime_c_api.h" "${ORT_INCLUDE_DIR}/dml_provider_factory.h" "${ORT_DLL}" "${DML_INCLUDE_DIR}/DirectML.h" "${DML_DLL}")
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "Expected file missing after extraction: ${_f}")
  endif()
endforeach()

# License texts that ship in the package next to the binaries.
configure_file("${ORT_PKG_DIR}/LICENSE" "${ORT_LICENSE_DIR}/ONNXRuntime-LICENSE.txt" COPYONLY)
configure_file("${ORT_PKG_DIR}/ThirdPartyNotices.txt" "${ORT_LICENSE_DIR}/ONNXRuntime-ThirdPartyNotices.txt" COPYONLY)
configure_file("${DML_PKG_DIR}/LICENSE.txt" "${ORT_LICENSE_DIR}/DirectML-LICENSE.txt" COPYONLY)
configure_file("${DML_PKG_DIR}/ThirdPartyNotices.txt" "${ORT_LICENSE_DIR}/DirectML-ThirdPartyNotices.txt" COPYONLY)
configure_file("${CMAKE_SOURCE_DIR}/third_party/licenses/LICENSE-Apache-2.0.txt" "${ORT_LICENSE_DIR}/DepthAnythingV2-LICENSE-Apache-2.0.txt" COPYONLY)

if(VDC_FETCH_DEPTH_MODEL)
  _vdc_download("${DEPTH_MODEL_URL}" "${DEPTH_MODEL_FILE}" "${DEPTH_MODEL_SHA256}")
  message(STATUS "Depth Anything V2 Small model ready: ${DEPTH_MODEL_FILE}")
else()
  message(STATUS "VDC_FETCH_DEPTH_MODEL=OFF: the depth model is not downloaded; estimated depth will be unavailable at run time")
endif()
message(STATUS "ONNX Runtime ${ORT_VERSION} + DirectML ${DML_VERSION} ready in ${ORT_ROOT}")
