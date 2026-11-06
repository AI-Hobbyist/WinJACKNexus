# SPDX-License-Identifier: AGPL-3.0-only
include(FetchContent)

# Keep dependency options local; do not change the host's testing/install flags.
function(wjn_bc_media_dependencies)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(OPUS_BUILD_SHARED_LIBRARY OFF)
    set(OPUS_BUILD_TESTING OFF)
    set(OPUS_BUILD_PROGRAMS OFF)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF)
    set(LIBSAMPLERATE_EXAMPLES OFF)
    set(LIBSAMPLERATE_INSTALL OFF)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_Declare(bc_opus
        URL https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz
        URL_HASH SHA256=6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_Declare(bc_samplerate
        URL https://github.com/libsndfile/libsamplerate/releases/download/0.2.2/libsamplerate-0.2.2.tar.xz
        URL_HASH SHA256=3258da280511d24b49d6b08615bbe824d0cacc9842b0e4caf11c52cf2b043893
        DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps"
        DOWNLOAD_NAME bc-libsamplerate-0.2.2.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(bc_opus bc_samplerate)
endfunction()
wjn_bc_media_dependencies()
