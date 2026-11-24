add_executable(imtg_identity_tests identifiers_tests.cpp)
add_test(NAME imtg_header_topology
    COMMAND ${CMAKE_COMMAND} -DIMTG_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckHeaderTopology.cmake)
target_link_libraries(imtg_identity_tests PRIVATE imtg::core)
add_test(NAME imtg_identity_tests COMMAND imtg_identity_tests)

add_executable(imtg_ui_tree_tests ui_tree_tests.cpp)
target_link_libraries(imtg_ui_tree_tests PRIVATE imtg::core)
add_test(NAME imtg_ui_tree_tests COMMAND imtg_ui_tree_tests)

add_executable(imtg_ui_allocation_tests ui_allocation_tests.cpp)
target_link_libraries(imtg_ui_allocation_tests PRIVATE imtg::core)
foreach(scenario IN ITEMS root text scope)
    add_test(NAME imtg_ui_allocation_${scenario} COMMAND imtg_ui_allocation_tests ${scenario})
    set_tests_properties(imtg_ui_allocation_${scenario} PROPERTIES TIMEOUT 15)
endforeach()

add_executable(imtg_action_registry_tests action_registry_tests.cpp)
target_link_libraries(imtg_action_registry_tests PRIVATE imtg::core)
add_test(NAME imtg_action_registry_tests COMMAND imtg_action_registry_tests)

add_executable(imtg_reference_runtime_tests reference_runtime_tests.cpp)
target_link_libraries(imtg_reference_runtime_tests PRIVATE imtg::core)
add_test(NAME imtg_reference_runtime_tests COMMAND imtg_reference_runtime_tests)

add_executable(imtg_callback_capture_lifetime_tests callback_capture_lifetime_tests.cpp)
target_link_libraries(imtg_callback_capture_lifetime_tests PRIVATE imtg::core)
foreach(scenario IN ITEMS reset close reset_quiescence close_quiescence
        reset_self_wait close_self_wait unregister action_in_flight
        subscribe_entry_failure subscribe_vector_failure register_allocation_sweep)
    add_test(NAME imtg_capture_${scenario}
        COMMAND imtg_callback_capture_lifetime_tests ${scenario})
    set_tests_properties(imtg_capture_${scenario} PROPERTIES TIMEOUT 10)
endforeach()

add_executable(imtg_storage_tests storage_tests.cpp)
target_link_libraries(imtg_storage_tests PRIVATE imtg::core)
if(IMTG_BUILD_MDBX)
    target_compile_definitions(imtg_storage_tests PRIVATE IMTG_TEST_MDBX=1)
    target_link_libraries(imtg_storage_tests PRIVATE imtg::mdbx)
endif()

if(IMTG_BUILD_TELEGRAM)
    add_executable(imtg_telegram_gateway_tests telegram_gateway_tests.cpp)
    target_link_libraries(imtg_telegram_gateway_tests PRIVATE imtg::telegram)
    add_test(NAME imtg_telegram_gateway_tests COMMAND imtg_telegram_gateway_tests)
    set_tests_properties(imtg_telegram_gateway_tests PROPERTIES TIMEOUT 30)
endif()
if(IMTG_BUILD_TELEGRAM_TRANSPORT)
    add_executable(imtg_telegram_transport_tests telegram_transport_tests.cpp)
    target_link_libraries(imtg_telegram_transport_tests PRIVATE imtg::telegram_transport)
    add_test(NAME imtg_telegram_transport_tests COMMAND imtg_telegram_transport_tests)
endif()

add_executable(imtg_logging_tests logging_tests.cpp)
target_link_libraries(imtg_logging_tests PRIVATE imtg::core log-it-cpp::log-it-cpp)
set_target_properties(imtg_logging_tests PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
if(IMTG_BUILD_TELEGRAM)
    target_compile_definitions(imtg_logging_tests PRIVATE IMTG_TEST_TELEGRAM=1)
    target_link_libraries(imtg_logging_tests PRIVATE imtg::telegram)
endif()
add_test(NAME imtg_logging_tests COMMAND imtg_logging_tests)
set_tests_properties(imtg_logging_tests PROPERTIES TIMEOUT 30)
add_test(NAME imtg_storage_tests COMMAND imtg_storage_tests)
if(IMTG_BUILD_TELEGRAM_KURLYK)
    if(NOT TARGET simple_web_server)
        if(NOT TARGET simple-web-server)
            if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/Simple-Web-Server/CMakeLists.txt")
                message(FATAL_ERROR "Kurlyk loopback tests require external/Simple-Web-Server or simple_web_server")
            endif()
            function(imtg_add_http_test_server)
                set(BUILD_TESTING OFF)
                set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
                add_subdirectory("${PROJECT_SOURCE_DIR}/external/Simple-Web-Server"
                    "${PROJECT_BINARY_DIR}/_deps/simple-web-server")
            endfunction()
            imtg_add_http_test_server()
        endif()
        add_library(simple_web_server ALIAS simple-web-server)
    endif()
    add_executable(imtg_kurlyk_http_transport_tests kurlyk_http_transport_tests.cpp)
    target_compile_definitions(imtg_kurlyk_http_transport_tests PRIVATE KURLYK_AUTO_INIT=0)
    target_link_libraries(imtg_kurlyk_http_transport_tests PRIVATE
        imtg::telegram_kurlyk ${imtg_kurlyk_target} simple_web_server)
    set_target_properties(imtg_kurlyk_http_transport_tests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
    if(IMTG_KURLYK_RUNTIME_DLLS)
        add_custom_command(TARGET imtg_kurlyk_http_transport_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMTG_KURLYK_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:imtg_kurlyk_http_transport_tests>)
    endif()
    add_test(NAME imtg_kurlyk_http_transport_tests COMMAND imtg_kurlyk_http_transport_tests)
    set_tests_properties(imtg_kurlyk_http_transport_tests PROPERTIES TIMEOUT 30)
    add_test(NAME imtg_kurlyk_installed_consumer
        COMMAND ${CMAKE_COMMAND}
            -DIMTG_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DIMTG_BINARY_DIR=${PROJECT_BINARY_DIR}
            -DIMTG_CONFIG=$<CONFIG>
            -DIMTG_COMMERCE=${IMTG_BUILD_COMMERCE}
            -DIMTG_MINGW=${MINGW}
            "-DIMTG_GENERATOR=${CMAKE_GENERATOR}"
            -DIMTG_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            "-DIMTG_RUNTIME_DLLS=${IMTG_KURLYK_RUNTIME_DLLS}"
            "-DIMTG_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
            "-DIMTG_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
            "-DCURL_INCLUDE_DIR=${CURL_INCLUDE_DIR}"
            "-DCURL_LIBRARY_RELEASE=${CURL_LIBRARY_RELEASE}"
            "-DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}"
            "-DOPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIBRARY}"
            "-DOPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIBRARY}"
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckKurlykInstalledConsumer.cmake)
    set_tests_properties(imtg_kurlyk_installed_consumer PROPERTIES TIMEOUT 300)
endif()

add_executable(imtg_session_tests session_tests.cpp)
target_link_libraries(imtg_session_tests PRIVATE imtg::core)
add_test(NAME imtg_session_tests COMMAND imtg_session_tests)

add_executable(imtg_render_intent_claim_tests render_intent_claim_tests.cpp)
target_link_libraries(imtg_render_intent_claim_tests PRIVATE imtg::core)
add_test(NAME imtg_render_intent_claim_tests COMMAND imtg_render_intent_claim_tests)

add_executable(imtg_reconciliation_tests reconciliation_tests.cpp)
target_link_libraries(imtg_reconciliation_tests PRIVATE imtg::core)
add_test(NAME imtg_reconciliation_tests COMMAND imtg_reconciliation_tests)

add_executable(imtg_outbox_delivery_tests outbox_delivery_tests.cpp)
target_link_libraries(imtg_outbox_delivery_tests PRIVATE imtg::core)
add_test(NAME imtg_outbox_delivery_tests COMMAND imtg_outbox_delivery_tests)

if(IMTG_BUILD_COMMERCE)
    add_executable(imtg_payment_contract_tests payment_contract_tests.cpp)
    target_link_libraries(imtg_payment_contract_tests PRIVATE imtg::commerce)
    add_test(NAME imtg_payment_contract_tests COMMAND imtg_payment_contract_tests)
endif()
if(IMTG_BUILD_MDBX)
    add_executable(imtg_mdbx_storage_tests mdbx_storage_tests.cpp)
    target_link_libraries(imtg_mdbx_storage_tests PRIVATE imtg::mdbx)
    if(MSVC)
        add_executable(imtg_msvc_noexcept_string_probe msvc_noexcept_string_probe.cpp)
        target_compile_features(imtg_msvc_noexcept_string_probe PRIVATE cxx_std_17)
        foreach(diagnostic_target IN ITEMS imtg_mdbx_storage_tests imtg_msvc_noexcept_string_probe)
            target_compile_options(${diagnostic_target} PRIVATE /Zi)
            target_link_options(${diagnostic_target} PRIVATE /DEBUG "/MAP:$<TARGET_FILE_DIR:${diagnostic_target}>/${diagnostic_target}.map")
        endforeach()
    endif()
    add_test(NAME imtg_mdbx_storage_tests COMMAND imtg_mdbx_storage_tests)
    set_tests_properties(imtg_mdbx_storage_tests PROPERTIES TIMEOUT 180)
    add_test(NAME imtg_flat_dependencies
        COMMAND ${CMAKE_COMMAND} -DIMTG_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckFlatDependencies.cmake)
endif()
