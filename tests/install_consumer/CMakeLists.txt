cmake_minimum_required(VERSION 3.16)
project(imtg_install_consumer LANGUAGES CXX)

find_package(imtg CONFIG REQUIRED)

add_executable(imtg_install_consumer main.cpp)
target_compile_features(imtg_install_consumer PRIVATE cxx_std_17)
target_link_libraries(imtg_install_consumer PRIVATE imtg::core)
