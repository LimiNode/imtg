add_executable(imtg_identity_tests identifiers_tests.cpp)
target_link_libraries(imtg_identity_tests PRIVATE imtg::core)
add_test(NAME imtg_identity_tests COMMAND imtg_identity_tests)

add_executable(imtg_ui_tree_tests ui_tree_tests.cpp)
target_link_libraries(imtg_ui_tree_tests PRIVATE imtg::core)
add_test(NAME imtg_ui_tree_tests COMMAND imtg_ui_tree_tests)
