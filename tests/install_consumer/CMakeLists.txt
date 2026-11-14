cmake_minimum_required(VERSION 3.16)
project(imtg_install_consumer LANGUAGES CXX)

find_package(imtg CONFIG REQUIRED)

# M2d-a deliberately publishes neither the adapter target nor its header.
if(TARGET imtg::mdbx)
    message(FATAL_ERROR "The source-only MDBX adapter leaked into the installed package")
endif()
get_target_property(imtg_include_dirs imtg::core INTERFACE_INCLUDE_DIRECTORIES)
foreach(include_dir IN LISTS imtg_include_dirs)
    if(EXISTS "${include_dir}/imtg/storage/mdbx_storage.hpp")
        message(FATAL_ERROR "The source-only MDBX header leaked into the installed package")
    endif()
endforeach()

add_executable(imtg_install_consumer main.cpp)
target_compile_features(imtg_install_consumer PRIVATE cxx_std_17)
target_link_libraries(imtg_install_consumer PRIVATE imtg::core)
