cmake_minimum_required(VERSION 3.21)
project(imtg_kurlyk_consumer LANGUAGES CXX)

find_package(imtg CONFIG REQUIRED COMPONENTS telegram_kurlyk)
add_executable(imtg_kurlyk_consumer main.cpp)
target_link_libraries(imtg_kurlyk_consumer PRIVATE imtg::telegram_kurlyk)
target_compile_features(imtg_kurlyk_consumer PRIVATE cxx_std_17)
if(IMTG_RUNTIME_DLLS)
    add_custom_command(TARGET imtg_kurlyk_consumer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMTG_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:imtg_kurlyk_consumer>)
endif()
enable_testing()
add_test(NAME imtg_kurlyk_consumer COMMAND imtg_kurlyk_consumer)
