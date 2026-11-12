cmake_minimum_required(VERSION 3.16)

project(imtg VERSION 0.1.0 LANGUAGES CXX)

option(IMTG_BUILD_DOCUMENTATION "Build the optional Doxygen documentation check" OFF)
option(IMTG_BUILD_COMMERCE "Build and install the optional imtg_commerce module" OFF)
option(IMTG_BUILD_MDBX "Build the optional persistent MDBX storage adapter" OFF)
set(IMTG_MDBX_CONTAINERS_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/external/mdbx-containers" CACHE PATH
    "Pinned mdbx-containers source used by the optional MDBX adapter")

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

add_library(imtg_core STATIC
    src/imtg/core/identifiers.cpp
    src/imtg/ui/tree.cpp
    src/imtg/actions/token.cpp
    src/imtg/actions/ActionRegistry.cpp
    src/imtg/runtime/reference_runtime.cpp
    src/imtg/storage/storage.cpp
    src/imtg/storage/time_encoding.cpp
    src/imtg/sessions/fsm.cpp
    src/imtg/sessions/session.cpp
    src/imtg/sessions/render_intents.cpp
    src/imtg/reconciliation/outbox.cpp)
add_library(imtg::core ALIAS imtg_core)
set_target_properties(imtg_core PROPERTIES EXPORT_NAME core)

target_compile_features(imtg_core PUBLIC cxx_std_17)
target_include_directories(imtg_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(IMTG_BUILD_COMMERCE)
    add_library(imtg_commerce STATIC
        src/imtg/commerce/payments.cpp)
    add_library(imtg::commerce ALIAS imtg_commerce)
    set_target_properties(imtg_commerce PROPERTIES EXPORT_NAME commerce)
    target_compile_features(imtg_commerce PUBLIC cxx_std_17)
    target_link_libraries(imtg_commerce PUBLIC imtg::core)
    target_include_directories(imtg_commerce PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endif()

if(IMTG_BUILD_MDBX)
    add_subdirectory(external)

    add_library(imtg_mdbx STATIC src/imtg/storage/mdbx_storage.cpp)
    add_library(imtg::mdbx ALIAS imtg_mdbx)
    set_target_properties(imtg_mdbx PROPERTIES EXPORT_NAME mdbx)
    target_compile_features(imtg_mdbx PUBLIC cxx_std_17)
    target_include_directories(imtg_mdbx PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(imtg_mdbx PUBLIC imtg::core
        mdbx_containers::mdbx_containers)
    # The source integration is intentionally not exported yet: exporting a
    # static target would also have to export the sibling libmdbx package and
    # its platform-specific runtime choices. M2d-b will define that install
    # boundary after the durable package contract is reviewed.
endif()

install(TARGETS imtg_core EXPORT imtgTargets)
if(IMTG_BUILD_COMMERCE)
    install(TARGETS imtg_commerce EXPORT imtgTargets)
endif()
install(DIRECTORY src/imtg/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/imtg
    FILES_MATCHING
    PATTERN "*.hpp"
    PATTERN "*.tpp"
    PATTERN "*.ipp"
    PATTERN "commerce" EXCLUDE
    PATTERN "commerce.hpp" EXCLUDE
    PATTERN "mdbx_storage.hpp" EXCLUDE)
if(IMTG_BUILD_COMMERCE)
    install(FILES src/imtg/commerce.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/imtg)
    install(DIRECTORY src/imtg/commerce/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/imtg/commerce
        FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.tpp"
        PATTERN "*.ipp")
endif()
install(EXPORT imtgTargets
    FILE imtgTargets.cmake
    NAMESPACE imtg::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imtg)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/imtgConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/imtgConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imtg)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/imtgConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/imtgConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/imtgConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imtg)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(IMTG_BUILD_DOCUMENTATION)
    find_package(Doxygen QUIET)
    if(DOXYGEN_FOUND)
        set(IMTG_DOXYGEN_OUTPUT_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}/docs" CACHE PATH
            "Output directory for the optional Doxygen check")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
            "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
            @ONLY)
        add_custom_target(imtg_documentation
            COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            COMMENT "Validating documented public imtg APIs with Doxygen"
            VERBATIM)
    else()
        message(WARNING
            "IMTG_BUILD_DOCUMENTATION is ON, but Doxygen was not found; "
            "the documentation target will not be available")
    endif()
endif()
