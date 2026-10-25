add_executable(imtg_core_tests identifiers_tests.cpp)
target_link_libraries(imtg_core_tests PRIVATE imtg::core)

add_test(NAME imtg_core_tests COMMAND imtg_core_tests)
