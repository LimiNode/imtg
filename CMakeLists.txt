cmake_minimum_required(VERSION 3.18)

project(imtg VERSION 0.1.0 LANGUAGES CXX)

option(IMTG_BUILD_DOCUMENTATION "Build the optional Doxygen documentation check" OFF)
option(IMTG_BUILD_COMMERCE "Build and install the optional imtg_commerce module" OFF)
option(IMTG_BUILD_MDBX "Build the optional persistent MDBX storage adapter" OFF)
option(IMTG_BUILD_TELEGRAM "Build the optional typed Telegram gateway boundary" OFF)
set(IMTG_MDBX_CONTAINERS_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/external/mdbx-containers" CACHE PATH
    "Pinned mdbx-containers source used by the optional MDBX adapter")

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)
include(cmake/ImtgLogitDependency.cmake)

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
set_target_properties(imtg_core PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
target_link_libraries(imtg_core PRIVATE log-it-cpp::log-it-cpp)
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
    if(WIN32)
        # Native headers must not turn std::min/max and numeric_limits into macros.
        target_compile_definitions(imtg_mdbx PRIVATE NOMINMAX)
    endif()
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

if(IMTG_BUILD_TELEGRAM)
    add_library(imtg_telegram STATIC
        src/imtg/telegram/UpdateRouter.cpp)
    add_library(imtg::telegram ALIAS imtg_telegram)
    set_target_properties(imtg_telegram PROPERTIES EXPORT_NAME telegram)
    target_compile_features(imtg_telegram PUBLIC cxx_std_17)
    set_target_properties(imtg_telegram PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
    target_include_directories(imtg_telegram PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(imtg_telegram PUBLIC imtg::core PRIVATE log-it-cpp::log-it-cpp)
endif()

install(TARGETS imtg_core EXPORT imtgTargets)
if(IMTG_BUILD_COMMERCE)
    install(TARGETS imtg_commerce EXPORT imtgTargets)
endif()
if(IMTG_BUILD_TELEGRAM)
    install(TARGETS imtg_telegram EXPORT imtgTargets)
endif()
include(cmake/ImtgPublicHeaders.cmake)
set(imtg_install_headers ${IMTG_CORE_PUBLIC_HEADERS})
if(IMTG_BUILD_COMMERCE)
    list(APPEND imtg_install_headers ${IMTG_COMMERCE_PUBLIC_HEADERS})
endif()
if(IMTG_BUILD_TELEGRAM)
    list(APPEND imtg_install_headers ${IMTG_TELEGRAM_PUBLIC_HEADERS})
endif()
foreach(header IN LISTS imtg_install_headers)
    get_filename_component(header_directory "${header}" DIRECTORY)
    install(FILES "src/${header}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${header_directory}")
endforeach()
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
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/imtg")

if(BUILD_TESTING)
    add_subdirectory(tests)
    foreach(check IN ITEMS InstalledConsumer Embedding)
        add_test(NAME imtg_logit_${check}
            COMMAND ${CMAKE_COMMAND}
                -DIMTG_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DIMTG_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DIMTG_CONFIG=$<CONFIG>
                "-DIMTG_GENERATOR=${CMAKE_GENERATOR}"
                -DIMTG_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                "-DIMTG_CONSUMER_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
                "-DIMTG_CONSUMER_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckLogit${check}.cmake)
        set_tests_properties(imtg_logit_${check} PROPERTIES TIMEOUT 180)
    endforeach()
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
