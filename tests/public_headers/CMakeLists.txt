cmake_minimum_required(VERSION 3.16)
project(imtg_public_headers LANGUAGES CXX)

find_package(imtg CONFIG REQUIRED)

set(IMTG_PUBLIC_HEADERS
    imtg/actions/actions.hpp
    imtg/actions/registry.hpp
    imtg/actions/token.hpp
    imtg/core/clock.hpp
    imtg/core/core.hpp
    imtg/core/identifiers.hpp
    imtg/core/result.hpp
    imtg/runtime/reference_runtime.hpp
    imtg/runtime/runtime.hpp
    imtg/storage/storage.hpp
    imtg/sessions/fsm.hpp
    imtg/sessions/session.hpp
    imtg/ui/tree.hpp
    imtg/ui/ui.hpp)

foreach(header IN LISTS IMTG_PUBLIC_HEADERS)
    string(REPLACE "/" "_" target_name "${header}")
    string(REPLACE "." "_" target_name "${target_name}")
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.cpp")
    file(WRITE "${source}" "#include <${header}>\nint main() { return 0; }\n")
    add_executable("${target_name}" "${source}")
    target_compile_features("${target_name}" PRIVATE cxx_std_17)
    target_link_libraries("${target_name}" PRIVATE imtg::core)
endforeach()
