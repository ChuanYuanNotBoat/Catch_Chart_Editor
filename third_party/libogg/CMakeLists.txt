# Minimal static build for the vendored libogg 1.3.5 sources.
# Upstream: https://github.com/xiph/ogg (tag v1.3.5), BSD-style license
# (see COPYING in this directory; installed with the application).
# Only the Ogg bitstream codec (bitwise/framing) is built; docs, tests,
# pkg-config and package-export plumbing from upstream are intentionally
# omitted because this tree is consumed via add_subdirectory() only.

include(CheckIncludeFiles)
check_include_files(inttypes.h INCLUDE_INTTYPES_H)
check_include_files(stdint.h INCLUDE_STDINT_H)
check_include_files(sys/types.h INCLUDE_SYS_TYPES_H)

set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
set(SIZE64 int64_t)
set(USIZE64 uint64_t)

configure_file(include/ogg/config_types.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/include/ogg/config_types.h @ONLY)

add_library(ogg STATIC
    src/bitwise.c
    src/framing.c
)
add_library(Ogg::ogg ALIAS ogg)

target_include_directories(ogg PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)
