cmake_minimum_required(VERSION 3.18)

foreach(mode IN ITEMS SOURCE IMPORTED PACKAGE NATIVE NORMAL CACHE PINNED MISSING_NATIVE MISSING_CONTAINERS)
    set(fixture_binary "${IMTG_BINARY_DIR}/mdbx-parent-${mode}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${IMTG_SOURCE_DIR}/tests/mdbx_parent"
            -B "${fixture_binary}" -G "${IMTG_GENERATOR}"
            -DIMTG_SOURCE_DIR=${IMTG_SOURCE_DIR} -DIMTG_PROVIDER_MODE=${mode}
            -DIMTG_NATIVE_SOURCE_DIR=${IMTG_NATIVE_SOURCE_DIR}
            -DIMTG_CONTAINER_SOURCE_DIR=${IMTG_CONTAINER_SOURCE_DIR}
            -DIMTG_LOGIT_PROVIDER_SOURCE_DIR=${IMTG_LOGIT_PROVIDER_SOURCE_DIR}
            -DIMTG_TIME_PROVIDER_SOURCE_DIR=${IMTG_TIME_PROVIDER_SOURCE_DIR}
            -DIMTG_LOGIT_SOURCE_DIR=${IMTG_LOGIT_PROVIDER_SOURCE_DIR}
            -DIMTG_TIME_SHIELD_SOURCE_DIR=${IMTG_TIME_PROVIDER_SOURCE_DIR}
            -DIMTG_TEST_PACKAGE_DIR=${IMTG_BINARY_DIR}/mdbx-parent-SOURCE/package
            -DCMAKE_C_COMPILER=${IMTG_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${IMTG_CXX_COMPILER}
            -DCMAKE_BUILD_TYPE=${IMTG_CONFIG}
            "-DCMAKE_CXX_FLAGS=${IMTG_CONSUMER_CXX_FLAGS}"
            "-DCMAKE_EXE_LINKER_FLAGS=${IMTG_CONSUMER_LINKER_FLAGS}"
        OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
    if(mode MATCHES "^MISSING_")
        if(result EQUAL 0)
            message(FATAL_ERROR "Absent ${mode} provider did not fail closed")
        endif()
        string(REGEX REPLACE "[ \t\r\n]+" " " diagnostics "${output}${error}")
        if(mode STREQUAL "MISSING_NATIVE" AND NOT diagnostics MATCHES "downloads are disabled")
            message(FATAL_ERROR "Unexpected native-provider failure: ${output}${error}")
        elseif(mode STREQUAL "MISSING_CONTAINERS"
                AND NOT diagnostics MATCHES "initialize external/mdbx-containers")
            message(FATAL_ERROR "Unexpected containers-provider failure: ${output}${error}")
        endif()
        continue()
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "MDBX ${mode} parent configure failed: ${output}${error}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${fixture_binary}"
        --config "${IMTG_CONFIG}" --target imtg_mdbx_parent_consumer --parallel 2 RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "MDBX ${mode} parent consumer build failed")
    endif()
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}"
        -C "${IMTG_CONFIG}" -R "^imtg_mdbx_parent_consumer$"
        --no-tests=error --output-on-failure WORKING_DIRECTORY "${fixture_binary}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "MDBX ${mode} parent consumer execution failed")
    endif()
endforeach()
