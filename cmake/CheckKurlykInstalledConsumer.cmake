cmake_minimum_required(VERSION 3.21)
set(prefix "${IMTG_BINARY_DIR}/kurlyk-installed-stage")
set(consumer "${IMTG_BINARY_DIR}/kurlyk-installed-consumer")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${IMTG_BINARY_DIR}"
    --config "${IMTG_CONFIG}" --prefix "${prefix}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Kurlyk adapter installation failed: ${result}")
endif()
set(provider_arguments)
foreach(variable CURL_INCLUDE_DIR CURL_LIBRARY_RELEASE OPENSSL_INCLUDE_DIR
        OPENSSL_SSL_LIBRARY OPENSSL_CRYPTO_LIBRARY)
    if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL ""
            AND NOT "${${variable}}" MATCHES "-NOTFOUND$")
        list(APPEND provider_arguments "-D${variable}=${${variable}}")
    endif()
endforeach()
if(MINGW OR IMTG_MINGW)
    if(OPENSSL_SSL_LIBRARY AND NOT OPENSSL_SSL_LIBRARY MATCHES "-NOTFOUND$")
        list(APPEND provider_arguments "-DSSL_EAY=${OPENSSL_SSL_LIBRARY}")
    endif()
    if(OPENSSL_CRYPTO_LIBRARY AND NOT OPENSSL_CRYPTO_LIBRARY MATCHES "-NOTFOUND$")
        list(APPEND provider_arguments "-DLIB_EAY=${OPENSSL_CRYPTO_LIBRARY}")
    endif()
endif()
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${IMTG_SOURCE_DIR}/tests/install_telegram_kurlyk_consumer" -B "${consumer}"
    -G "${IMTG_GENERATOR}" -DCMAKE_CXX_COMPILER=${IMTG_CXX_COMPILER}
    -DCMAKE_BUILD_TYPE=${IMTG_CONFIG} -DCMAKE_PREFIX_PATH=${prefix}
    "-DIMTG_RUNTIME_DLLS=${IMTG_RUNTIME_DLLS}"
    "-DCMAKE_CXX_FLAGS=${IMTG_CXX_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${IMTG_LINKER_FLAGS}"
    ${provider_arguments} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed kurlyk adapter consumer configure failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer}"
    --config "${IMTG_CONFIG}" --parallel 2 RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed kurlyk adapter consumer build failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer}"
    -C "${IMTG_CONFIG}" --no-tests=error --output-on-failure RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed kurlyk adapter consumer failed: ${result}")
endif()

set(headers "${IMTG_BINARY_DIR}/kurlyk-installed-headers")
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${IMTG_SOURCE_DIR}/tests/public_headers" -B "${headers}"
    -G "${IMTG_GENERATOR}" -DCMAKE_CXX_COMPILER=${IMTG_CXX_COMPILER}
    -DCMAKE_BUILD_TYPE=${IMTG_CONFIG} -DCMAKE_PREFIX_PATH=${prefix}
    -DIMTG_REQUIRE_COMMERCE=${IMTG_COMMERCE}
    -DIMTG_REQUIRE_TELEGRAM=ON -DIMTG_REQUIRE_TELEGRAM_TRANSPORT=ON
    -DIMTG_REQUIRE_TELEGRAM_KURLYK=ON
    "-DCMAKE_CXX_FLAGS=${IMTG_CXX_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${IMTG_LINKER_FLAGS}"
    ${provider_arguments} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed kurlyk header checks configure failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${headers}"
    --config "${IMTG_CONFIG}" --parallel 2 RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed kurlyk header checks failed: ${result}")
endif()

set(parent "${IMTG_BINARY_DIR}/kurlyk-parent-reuse")
execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${IMTG_SOURCE_DIR}/tests/parent_telegram_kurlyk" -B "${parent}"
    -G "${IMTG_GENERATOR}" -DCMAKE_CXX_COMPILER=${IMTG_CXX_COMPILER}
    -DCMAKE_BUILD_TYPE=${IMTG_CONFIG} -DCMAKE_PREFIX_PATH=${prefix}
    -DIMTG_SOURCE_DIR=${IMTG_SOURCE_DIR}
    "-DIMTG_RUNTIME_DLLS=${IMTG_RUNTIME_DLLS}"
    "-DCMAKE_CXX_FLAGS=${IMTG_CXX_FLAGS}"
    "-DCMAKE_EXE_LINKER_FLAGS=${IMTG_LINKER_FLAGS}"
    ${provider_arguments} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Parent-owned kurlyk target configure failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${parent}"
    --config "${IMTG_CONFIG}" --parallel 2 RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Parent-owned kurlyk target build failed: ${result}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${parent}"
    -C "${IMTG_CONFIG}" --no-tests=error --output-on-failure RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Parent-owned kurlyk target consumer failed: ${result}")
endif()
