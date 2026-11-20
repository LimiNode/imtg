cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED IMTG_SOURCE_DIR OR NOT DEFINED IMTG_BINARY_DIR)
    message(FATAL_ERROR "Set IMTG_SOURCE_DIR and IMTG_BINARY_DIR")
endif()

set(install_prefix "${IMTG_BINARY_DIR}/logit-install-test")
set(consumer_binary_dir "${IMTG_BINARY_DIR}/logit-consumer-test")
if(NOT IMTG_CONFIG)
    set(IMTG_CONFIG Debug)
endif()

# The prefix is a task-local generated directory. Remove it so a renamed or
# removed optional header cannot survive from an earlier test invocation.
get_filename_component(binary_absolute "${IMTG_BINARY_DIR}" ABSOLUTE)
get_filename_component(prefix_parent "${install_prefix}" DIRECTORY)
get_filename_component(prefix_parent "${prefix_parent}" ABSOLUTE)
if(NOT prefix_parent STREQUAL binary_absolute)
    message(FATAL_ERROR "Install stage must stay directly inside the build directory")
endif()
file(REMOVE_RECURSE "${install_prefix}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${IMTG_BINARY_DIR}" --config "${IMTG_CONFIG}" --prefix "${install_prefix}"
    RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "LogIt++ install failed: ${install_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${IMTG_SOURCE_DIR}/tests/logit_install_consumer"
        -B "${consumer_binary_dir}" -G "${IMTG_GENERATOR}" -DCMAKE_PREFIX_PATH=${install_prefix}
        -DCMAKE_CXX_COMPILER=${IMTG_CXX_COMPILER}
        -DCMAKE_BUILD_TYPE=${IMTG_CONFIG}
        "-DCMAKE_CXX_FLAGS=${IMTG_CONSUMER_CXX_FLAGS}"
        "-DCMAKE_EXE_LINKER_FLAGS=${IMTG_CONSUMER_LINKER_FLAGS}"
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Installed LogIt++ consumer configure failed: ${configure_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_binary_dir}" --config "${IMTG_CONFIG}" --parallel
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Installed LogIt++ consumer build failed: ${build_result}")
endif()

execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_binary_dir}" -C "${IMTG_CONFIG}" --no-tests=error --output-on-failure
    RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "Installed LogIt++ consumer test failed: ${test_result}")
endif()
