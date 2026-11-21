cmake_minimum_required(VERSION 3.18)
foreach(mode IN ITEMS ON OFF NORMAL CACHE)
    set(fixture_binary "${IMTG_BINARY_DIR}/logit-parent-${mode}")
    set(parent_options -DIMTG_PARENT_PROVIDES_LOGIT=${mode}
        -DIMTG_PARENT_DEPENDENCY_SETTINGS=PROVIDED)
    if(mode STREQUAL "NORMAL" OR mode STREQUAL "CACHE")
        set(parent_options -DIMTG_PARENT_PROVIDES_LOGIT=OFF
            -DIMTG_PARENT_DEPENDENCY_SETTINGS=${mode})
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${IMTG_SOURCE_DIR}/tests/logit_parent"
            -B "${fixture_binary}" -G "${IMTG_GENERATOR}"
            -DIMTG_SOURCE_DIR=${IMTG_SOURCE_DIR}
            ${parent_options}
            -DCMAKE_CXX_COMPILER=${IMTG_CXX_COMPILER}
            -DCMAKE_BUILD_TYPE=${IMTG_CONFIG}
            "-DCMAKE_CXX_FLAGS=${IMTG_CONSUMER_CXX_FLAGS}"
            "-DCMAKE_EXE_LINKER_FLAGS=${IMTG_CONSUMER_LINKER_FLAGS}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Parent-target reuse configure failed: ${result}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${fixture_binary}"
        --config "${IMTG_CONFIG}" --target imtg_parent_consumer --parallel 2 RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Parent-target reuse build failed: ${result}")
    endif()
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${fixture_binary}"
        -C "${IMTG_CONFIG}" -R "^imtg_parent_consumer$"
        --no-tests=error --output-on-failure RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Parent-target reuse executable failed: ${result}")
    endif()
endforeach()
