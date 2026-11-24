cmake_minimum_required(VERSION 3.16)
project(imtg_install_commerce_consumer LANGUAGES CXX)

include(CTest)

find_package(imtg CONFIG REQUIRED)

add_executable(imtg_install_commerce_consumer main.cpp)
if(MINGW)
    # Avoid Windows installer detection requesting elevation for an unmanifested test.
    set_target_properties(imtg_install_commerce_consumer PROPERTIES OUTPUT_NAME imtg_commerce_consumer)
endif()
target_compile_features(imtg_install_commerce_consumer PRIVATE cxx_std_17)
target_link_libraries(imtg_install_commerce_consumer PRIVATE imtg::commerce)
add_test(NAME imtg_install_commerce_consumer_smoke COMMAND imtg_install_commerce_consumer)
