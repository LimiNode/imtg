cmake_minimum_required(VERSION 3.16)
project(imtg_public_headers LANGUAGES CXX)

find_package(imtg CONFIG REQUIRED)

include(../../cmake/ImtgPublicHeaders.cmake)
set(IMTG_PUBLIC_HEADERS ${IMTG_CORE_PUBLIC_HEADERS})
option(IMTG_REQUIRE_COMMERCE "Check optional commerce headers and target" OFF)
if(IMTG_REQUIRE_COMMERCE)
    list(APPEND IMTG_PUBLIC_HEADERS ${IMTG_COMMERCE_PUBLIC_HEADERS})
endif()

# Compare the real package to the declared manifest, not just the source tree.
get_target_property(include_dirs imtg::core INTERFACE_INCLUDE_DIRECTORIES)
list(REMOVE_DUPLICATES include_dirs)
foreach(include_dir IN LISTS include_dirs)
    file(GLOB_RECURSE installed_headers RELATIVE "${include_dir}"
        "${include_dir}/imtg/*.hpp" "${include_dir}/imtg/*.tpp" "${include_dir}/imtg/*.ipp")
    list(SORT installed_headers)
    set(expected_headers ${IMTG_PUBLIC_HEADERS})
    list(SORT expected_headers)
    if(NOT "${installed_headers}" STREQUAL "${expected_headers}")
        message(FATAL_ERROR "Installed public headers differ from the manifest: ${installed_headers}")
    endif()
endforeach()

foreach(header IN LISTS IMTG_PUBLIC_HEADERS)
    string(REPLACE "/" "_" target_name "${header}")
    string(REPLACE "." "_" target_name "${target_name}")
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${target_name}.cpp")
    if(header STREQUAL "imtg/actions/ActionHandle.hpp")
        file(WRITE "${source}" "#include <${header}>\nint main() { imtg::ActionHandle<int> handle; return handle.validate() ? 1 : 0; }\n")
    elseif(header STREQUAL "imtg/actions/ActionRegistry.hpp")
        file(WRITE "${source}" [=[
#include <imtg/actions/ActionRegistry.hpp>
#include <string>

struct Payload { int value{}; };

int main()
{
    imtg::ManualClock clock(imtg::ClockTime{});
    imtg::PlainTokenCodec token_codec;
    imtg::ActionRegistry registry(token_codec, clock);
    const auto registered = registry.register_action<Payload>(
        imtg::ActionId{"standalone.v1"},
        imtg::PayloadCodec<Payload>{
            1,
            [](const Payload& payload) -> imtg::Result<std::string> {
                return std::to_string(payload.value);
            },
            [](std::string_view) -> imtg::Result<Payload> { return Payload{}; }},
        [](imtg::ActionContext&, const Payload&) { return imtg::Result<void>(); });
    if (!registered) {
        return 1;
    }
    return registered.value().validate() ? 0 : 1;
}
]=])
    else()
        file(WRITE "${source}" "#include <${header}>\nint main() { return 0; }\n")
    endif()
    add_executable("${target_name}" "${source}")
    target_compile_features("${target_name}" PRIVATE cxx_std_17)
    if(header MATCHES "^imtg/commerce(/|[.])")
        target_link_libraries("${target_name}" PRIVATE imtg::commerce)
    else()
        target_link_libraries("${target_name}" PRIVATE imtg::core)
    endif()
endforeach()
