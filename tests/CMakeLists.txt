add_executable(imtg_core_tests identifiers_tests.cpp)
target_link_libraries(imtg_core_tests PRIVATE imtg::core)
target_sources(imtg_core_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include/imtg/core/identifiers.cpp)

add_test(NAME imtg_core_tests COMMAND imtg_core_tests)
