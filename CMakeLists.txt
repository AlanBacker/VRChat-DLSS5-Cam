cmake_minimum_required(VERSION 3.21)
project(VRChatDLSS5Cam VERSION 1.4.4 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "VRChat DLSS5 Cam targets Windows only.")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "VRChat DLSS5 Cam must be built with MSVC (the NVIDIA NGX static library is MSVC-only).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# A build published as a pre-release says so on its start-up card and in About. Switch it off for a full release.
option(APP_PRERELEASE "Mark the build as a pre-release" ON)
option(APP_EDITION_AMD "Build the Radeon edition (FSR host route for DLSS-NR-on-AMD, none of the NVIDIA-only files in the package)" OFF)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- NVIDIA DLSS SDK (headers + NGX static library + DLSS runtime) -------------
# Downloaded at configure time from NVIDIA's public GitHub repository. The
# leaked DLSS 5 runtime (nvngx_dlssnr.dll) is NEVER downloaded or shipped.
include(cmake/FetchNGX.cmake)
include(cmake/FetchFidelityFX.cmake)

# --- ONNX Runtime (DirectML) + Depth Anything V2 Small ------------------------
# Monocular depth estimation for the frame-guidance stage (the neural renderer
# wants a depth buffer, a Spout stream has none). Downloaded from NuGet / Hugging Face.
include(cmake/FetchOnnxRuntime.cmake)

# --- Third party (vendored) ----------------------------------------------------
add_library(imgui STATIC
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_dx12.cpp
  third_party/imgui/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DEFINE_MATH_OPERATORS UNICODE _UNICODE)

add_library(spout STATIC
  third_party/spout/SpoutDX.cpp
  third_party/spout/SpoutCopy.cpp
  third_party/spout/SpoutDirectX.cpp
  third_party/spout/SpoutFrameCount.cpp
  third_party/spout/SpoutSenderNames.cpp
  third_party/spout/SpoutSharedMemory.cpp
  third_party/spout/SpoutUtils.cpp)
target_include_directories(spout PUBLIC third_party/spout)
target_compile_definitions(spout PUBLIC UNICODE _UNICODE)
target_compile_options(spout PRIVATE /wd4267 /wd4244 /wd4996)

# --- Application ---------------------------------------------------------------
set(APP_SOURCES
  src/main.cpp
  src/core/App.cpp
  src/core/Util.cpp
  src/core/Updater.cpp
  src/core/Splash.cpp
  src/core/Log.cpp
  src/core/Settings.cpp
  src/core/I18n.cpp
  src/core/Capture.cpp
  src/core/SpoutReceiver.cpp
  src/core/ImageSource.cpp
  src/core/MediaFoundation.cpp
  src/core/VideoSource.cpp
  src/core/VideoWriter.cpp
  src/core/MediaLibrary.cpp
  src/gfx/Device.cpp
  src/gfx/ThumbnailAtlas.cpp
  src/gfx/Shaders.cpp
  src/gfx/Pipeline.cpp
  src/gfx/FsrHost.cpp
  src/gfx/NvOpticalFlow.cpp
  src/gfx/DepthEstimator.cpp
  src/ngx/NgxCore.cpp
  src/ngx/DlssnrFeature.cpp
  src/ngx/DlaaFeature.cpp
  src/ui/Theme.cpp
  src/ui/Fonts.cpp
  src/ui/MainUI.cpp)

# The .manifest source is merged by CMake into the linker-generated manifest (per-monitor DPI, UTF-8 code page).
add_executable(VRChatDLSS5Cam WIN32 ${APP_SOURCES} resources/app.rc resources/app.manifest)
target_include_directories(VRChatDLSS5Cam PRIVATE src third_party/nvof ${NGX_INCLUDE_DIR} ${FFX_INCLUDE_DIR} ${ORT_INCLUDE_DIR} ${DML_INCLUDE_DIR})
target_compile_definitions(VRChatDLSS5Cam PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
  APP_VERSION_STRING="${PROJECT_VERSION}"
  APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  APP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
  APP_PRERELEASE=$<BOOL:${APP_PRERELEASE}>
  APP_EDITION_AMD=$<BOOL:${APP_EDITION_AMD}>)
target_compile_options(VRChatDLSS5Cam PRIVATE /W4 /MP /utf-8 /permissive- /EHsc /wd4100 /wd4201 /wd4324)
target_link_libraries(VRChatDLSS5Cam PRIVATE
  imgui spout ${NGX_STATIC_LIB}
  d3d12 d3d11 dxgi d3dcompiler dxguid windowscodecs mfuuid
  ole32 shell32 shlwapi user32 gdi32 dwmapi comctl32 advapi32 psapi version winmm winhttp)
set_target_properties(VRChatDLSS5Cam PROPERTIES OUTPUT_NAME "VRChatDLSS5Cam")

# Copy the DLSS super-resolution runtime next to the executable (used by the
# optional DLAA pre-pass; NGX loads feature DLLs from the executable folder).
add_custom_command(TARGET VRChatDLSS5Cam POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NGX_DLSS_DLL}" "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/nvngx_dlss.dll"
  COMMENT "Copying nvngx_dlss.dll next to the executable")

# ONNX Runtime + DirectML are loaded dynamically at run time from the executable
# folder; the depth model lives in models\ and the licenses in licenses\.
add_custom_command(TARGET VRChatDLSS5Cam POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DLL}" "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/onnxruntime.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_PROVIDERS_SHARED_DLL}" "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/onnxruntime_providers_shared.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DML_DLL}" "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/DirectML.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${ORT_LICENSE_DIR}" "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/licenses"
  COMMENT "Copying ONNX Runtime, DirectML and license files next to the executable")
if(VDC_FETCH_DEPTH_MODEL)
  add_custom_command(TARGET VRChatDLSS5Cam POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DEPTH_MODEL_FILE}" "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/models/depth_anything_v2_small_fp16.onnx"
    COMMENT "Copying the Depth Anything V2 Small model")
endif()

# The FidelityFX API runtime (FSR host route) and its license next to the executable.
add_custom_command(TARGET VRChatDLSS5Cam POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFX_DX12_DLL}" "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/amd_fidelityfx_dx12.dll"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFX_LICENSE_FILE}" "$<TARGET_FILE_DIR:VRChatDLSS5Cam>/licenses/FidelityFX-SDK-LICENSE.txt")

# --- Packaging -----------------------------------------------------------------
install(TARGETS VRChatDLSS5Cam RUNTIME DESTINATION .)
install(FILES "${NGX_DLSS_DLL}" "${ORT_DLL}" "${ORT_PROVIDERS_SHARED_DLL}" "${DML_DLL}" DESTINATION .)
install(DIRECTORY "${ORT_LICENSE_DIR}/" DESTINATION licenses)
install(FILES "${FFX_DX12_DLL}" DESTINATION .)
install(FILES "${FFX_LICENSE_FILE}" DESTINATION licenses RENAME FidelityFX-SDK-LICENSE.txt)
if(VDC_FETCH_DEPTH_MODEL)
  install(FILES "${DEPTH_MODEL_FILE}" DESTINATION models)
endif()
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
install(DIRECTORY docs/ DESTINATION docs)
