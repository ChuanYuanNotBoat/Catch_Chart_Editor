# Minimal static build for the vendored libvorbis 1.3.7 sources.
# Upstream: https://github.com/xiph/vorbis (tag v1.3.7), BSD-style license
# (see COPYING in this directory; installed with the application).
# Builds the full encoder/decoder library (including vorbisenc) as a single
# static target consumed via add_subdirectory(); upstream's shared-library
# exports, pkg-config files, examples and tests are intentionally omitted.

add_library(vorbis STATIC
    lib/mdct.c
    lib/smallft.c
    lib/block.c
    lib/envelope.c
    lib/window.c
    lib/lsp.c
    lib/lpc.c
    lib/analysis.c
    lib/synthesis.c
    lib/psy.c
    lib/info.c
    lib/floor1.c
    lib/floor0.c
    lib/res0.c
    lib/mapping0.c
    lib/registry.c
    lib/codebook.c
    lib/sharedbook.c
    lib/lookup.c
    lib/bitrate.c
    lib/vorbisenc.c
)
add_library(Vorbis::vorbis ALIAS vorbis)

target_include_directories(vorbis
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(vorbis PUBLIC Ogg::ogg)

if(MSVC)
    target_compile_definitions(vorbis PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_SECURE_NO_DEPRECATE
        _CRT_NONSTDC_NO_DEPRECATE
    )
endif()
