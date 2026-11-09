cmake_minimum_required(VERSION 3.16)
project(imtg_public_headers LANGUAGES CXX)

find_package(imtg CONFIG REQUIRED)

set(IMTG_PUBLIC_HEADERS
    imtg/actions.hpp
    imtg/actions/actions.hpp
    imtg/actions/ActionCommand.hpp
    imtg/actions/ActionContext.hpp
    imtg/actions/ActionHandle.hpp
    imtg/actions/ActionRegistry.hpp
    imtg/actions/PayloadCodec.hpp
    imtg/actions/registry.hpp
    imtg/actions/token.hpp
    imtg/core/clock.hpp
    imtg/core/core.hpp
    imtg/core/identifiers.hpp
    imtg/core/result.hpp
    imtg/runtime/reference_runtime.hpp
    imtg/runtime/runtime.hpp
    imtg/storage/storage.hpp
    imtg/storage/time_encoding.hpp
    imtg/sessions/fsm.hpp
    imtg/sessions/render_intents.hpp
    imtg/sessions/session.hpp
    imtg/reconciliation/outbox.hpp
    imtg/ui/tree.hpp
    imtg/ui/ui.hpp)

option(IMTG_REQUIRE_COMMERCE "Check optional commerce headers and target" OFF)
if(IMTG_REQUIRE_COMMERCE)
    list(APPEND IMTG_PUBLIC_HEADERS
        imtg/commerce.hpp
        imtg/commerce/PaymentTypes.hpp
        imtg/commerce/PaymentPolicy.hpp
        imtg/commerce/PaymentRecords.hpp
        imtg/commerce/PaymentTransaction.hpp
        imtg/commerce/PaymentProvider.hpp
        imtg/commerce/PaymentServices.hpp)
endif()

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
    if(header MATCHES "^imtg/commerce/")
        target_link_libraries("${target_name}" PRIVATE imtg::commerce)
    else()
        target_link_libraries("${target_name}" PRIVATE imtg::core)
    endif()
endforeach()
