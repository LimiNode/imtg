if(NOT DEFINED IMTG_SOURCE_DIR OR NOT DEFINED IMTG_INSTALL_PREFIX)
    message(FATAL_ERROR
        "IMTG_SOURCE_DIR and IMTG_INSTALL_PREFIX are required")
endif()

set(source_license "${IMTG_SOURCE_DIR}/LICENSE")
set(installed_license
    "${IMTG_INSTALL_PREFIX}/share/licenses/imtg/LICENSE")

if(NOT EXISTS "${source_license}")
    message(FATAL_ERROR "Root license is missing: ${source_license}")
endif()
if(NOT EXISTS "${installed_license}")
    message(FATAL_ERROR "Installed license is missing: ${installed_license}")
endif()

file(SHA256 "${source_license}" source_digest)
file(SHA256 "${installed_license}" installed_digest)
if(NOT source_digest STREQUAL installed_digest)
    message(FATAL_ERROR
        "Installed license differs from the root LICENSE file")
endif()

message(STATUS "Installed license matches ${source_license}")
