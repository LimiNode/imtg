cmake_minimum_required(VERSION 3.18)
project(imtg_logit_install_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(imtg CONFIG REQUIRED)

add_executable(imtg_logit_consumer main.cpp)
target_link_libraries(imtg_logit_consumer PRIVATE imtg::core log-it-cpp::log-it-cpp)
enable_testing()
add_test(NAME imtg_logit_install_consumer_smoke COMMAND imtg_logit_consumer)
