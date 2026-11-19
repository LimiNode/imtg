cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED IMTG_SOURCE_DIR OR NOT DEFINED IMTG_BINARY_DIR)
    message(FATAL_ERROR "Set IMTG_SOURCE_DIR and IMTG_BINARY_DIR")
endif()

set(install_prefix "${IMTG_BINARY_DIR}/logit-install-test")
set(consumer_binary_dir "${IMTG_BINARY_DIR}/logit-consumer-test")

# The prefix is a task-local generated directory. Remove it so a renamed or
# removed optional header cannot survive from an earlier test invocation.
file(REMOVE_RECURSE "${install_prefix}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${IMTG_BINARY_DIR}" --prefix "${install_prefix}"
    RESULT_VARIABLE install_result)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Optional LogIt++ install failed: ${install_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${IMTG_SOURCE_DIR}/tests/logit_install_consumer"
        -B "${consumer_binary_dir}" -DCMAKE_PREFIX_PATH=${install_prefix}
    RESULT_VARIABLE configure_result)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Installed LogIt++ consumer configure failed: ${configure_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_binary_dir}" --parallel
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Installed LogIt++ consumer build failed: ${build_result}")
endif()

execute_process(
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_binary_dir}" --output-on-failure
    RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "Installed LogIt++ consumer test failed: ${test_result}")
endif()
