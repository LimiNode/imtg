cmake_minimum_required(VERSION 3.18)
project(imtg_logit_install_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(imtg CONFIG REQUIRED COMPONENTS logit)
if(NOT TARGET imtg::logit)
    message(FATAL_ERROR "The optional imtg::logit target is missing from the installed package")
endif()
find_package(log-it-cpp 1.0.2 CONFIG REQUIRED)

add_executable(imtg_logit_consumer main.cpp)
target_link_libraries(imtg_logit_consumer PRIVATE imtg::logit)
enable_testing()
add_test(NAME imtg_logit_install_consumer_smoke COMMAND imtg_logit_consumer)
