# Fetches libwebp (the WebP reference library by Google) from its GitHub mirror at configure time and builds the
# decoder, encoder, demux and mux libraries as static libraries. Pinned to the v1.6.0 tag and verified by SHA-256.
# BSD 3-Clause License, Copyright (c) 2010, Google Inc. (see THIRD_PARTY_NOTICES.md).
#
# Animated WebP files are decoded and written with it (src/core/AnimatedImage.cpp), and still WebP pictures are
# decoded with it when Windows has no WebP codec installed.

include(FetchContent)

set(LIBWEBP_TAG "v1.6.0" CACHE STRING "libwebp tag to fetch")
set(LIBWEBP_SHA256 "93a852c2b3efafee3723efd4636de855b46f9fe1efddd607e1f42f60fc8f2136" CACHE STRING "SHA-256 of the libwebp source archive")

set(WEBP_LINK_STATIC ON CACHE BOOL "" FORCE)
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_USE_THREAD ON CACHE BOOL "" FORCE)
set(WEBP_ENABLE_SIMD ON CACHE BOOL "" FORCE)
set(WEBP_UNICODE ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(libwebp
  URL "https://github.com/webmproject/libwebp/archive/refs/tags/${LIBWEBP_TAG}.tar.gz"
  URL_HASH "SHA256=${LIBWEBP_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libwebp)

set(LIBWEBP_INCLUDE_DIR "${libwebp_SOURCE_DIR}/src")
set(LIBWEBP_LICENSE_FILE "${libwebp_SOURCE_DIR}/COPYING")
set(LIBWEBP_LIBRARIES webp webpdemux libwebpmux sharpyuv)
