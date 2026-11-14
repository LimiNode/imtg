# Only direct, pinned sibling dependencies belong to this provider.
# SYSTEM mode below accepts our existing target but forbids nested/download fallback.
if(NOT EXISTS "${IMTG_MDBX_CONTAINERS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "IMTG_BUILD_MDBX requires the pinned external/mdbx-containers submodule")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libmdbx/CMakeLists.txt")
    message(FATAL_ERROR
        "IMTG_BUILD_MDBX requires the pinned external/libmdbx submodule")
endif()
# Provide the flat sibling dependency first so mdbx-containers never
# falls back to a configure-time FetchContent download.
set(MDBX_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(MDBX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MDBX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
if(MSVC)
    # The pinned MDBX selects C23 when advertised, but MSVC rejects its
    # C23 maybe_unused declaration. C11 avoids that unsupported path.
    set(MDBX_C_STANDARD 11)
endif()
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/libmdbx"
    "${CMAKE_BINARY_DIR}/_deps/libmdbx" EXCLUDE_FROM_ALL)
if(MSVC)
    # Upstream exports this option but its own C++ wrapper also needs it.
    target_compile_options(mdbx-static PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
endif()
# Keep the sibling project as a dependency provider only. Its own tests,
# examples and optional transports are outside imtg's build graph.
set(MDBXC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MDBXC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MDBXC_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(MDBXC_DEPS_MODE SYSTEM CACHE STRING "" FORCE)
if(NOT TARGET mdbx-static)
    message(FATAL_ERROR "The flat libmdbx provider did not create mdbx-static")
endif()

add_subdirectory("${IMTG_MDBX_CONTAINERS_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/mdbx-containers" EXCLUDE_FROM_ALL)
