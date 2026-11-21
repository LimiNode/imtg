# Parent-owned targets win before unused source paths. A parent may explicitly
# find_package first; imtg never substitutes an implicit package for pinned sources.
if(TARGET mdbx_containers::mdbx_containers)
    return()
endif()

# Preserve normal variables in both owned providers under older parent policies.
if(POLICY CMP0077)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
endif()
if(POLICY CMP0126)
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
endif()

if(NOT DEFINED IMTG_MDBX_CONTAINERS_SOURCE_DIR)
    set(IMTG_MDBX_CONTAINERS_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/mdbx-containers" CACHE PATH
        "Pinned mdbx-containers source used when no target is supplied")
endif()
if(NOT EXISTS "${IMTG_MDBX_CONTAINERS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Provide mdbx_containers::mdbx_containers or initialize external/mdbx-containers")
endif()

# These are the existing target names accepted by the pinned containers provider.
set(imtg_native_mdbx_targets mdbx::mdbx mdbx::mdbx-static mdbx mdbx-static
    libmdbx::mdbx libmdbx::mdbx-static libmdbx libmdbx-static mdbx_static libmdbx_static)
set(imtg_has_native_mdbx FALSE)
foreach(candidate IN LISTS imtg_native_mdbx_targets)
    if(TARGET ${candidate})
        set(imtg_has_native_mdbx TRUE)
        break()
    endif()
endforeach()
if(NOT imtg_has_native_mdbx)
    if(NOT DEFINED IMTG_LIBMDBX_SOURCE_DIR)
        set(IMTG_LIBMDBX_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libmdbx" CACHE PATH
            "Pinned libmdbx source used when no target is supplied")
    endif()
    if(NOT EXISTS "${IMTG_LIBMDBX_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "Provide an MDBX target or initialize external/libmdbx; downloads are disabled")
    endif()
    foreach(setting IN ITEMS MDBX_BUILD_SHARED_LIBRARY MDBX_BUILD_TOOLS MDBX_ENABLE_TESTS)
        if(NOT DEFINED ${setting})
            set(${setting} OFF CACHE BOOL "Build optional libmdbx components")
        endif()
    endforeach()
    if(MSVC AND NOT DEFINED MDBX_C_STANDARD)
        # The pinned libmdbx C23 declaration is not supported by MSVC.
        set(MDBX_C_STANDARD 11)
    endif()
    add_subdirectory("${IMTG_LIBMDBX_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/_deps/libmdbx" EXCLUDE_FROM_ALL)
    if(NOT TARGET mdbx-static)
        message(FATAL_ERROR "The libmdbx source provider did not create mdbx-static")
    endif()
    if(MSVC)
        target_compile_options(mdbx-static PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
    endif()
endif()

foreach(setting IN ITEMS MDBXC_BUILD_EXAMPLES MDBXC_BUILD_TESTS MDBXC_BUILD_BENCHMARKS)
    if(NOT DEFINED ${setting})
        set(${setting} OFF CACHE BOOL "Build optional mdbx-containers components")
    endif()
endforeach()
if(NOT DEFINED MDBXC_DEPS_MODE)
    set(MDBXC_DEPS_MODE SYSTEM)
endif()
# A typed cache entry prevents old CMake from dropping a parent's normal value
# when the containers provider repeats set(CACHE); restore our local binding too.
set(imtg_mdbxc_deps_mode "${MDBXC_DEPS_MODE}")
set(MDBXC_DEPS_MODE "${imtg_mdbxc_deps_mode}" CACHE STRING "MDBX dependency resolution mode")
set(MDBXC_DEPS_MODE "${imtg_mdbxc_deps_mode}")
# A native target is already resolved, so even a parent's AUTO/BUNDLED mode
# cannot reach the containers provider's nested-source or download fallback.
add_subdirectory("${IMTG_MDBX_CONTAINERS_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/mdbx-containers" EXCLUDE_FROM_ALL)
if(NOT TARGET mdbx_containers::mdbx_containers)
    message(FATAL_ERROR "The containers source provider did not create its public target")
endif()
