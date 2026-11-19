cmake_minimum_required(VERSION 3.16)
project(imtg_telegram_transport_consumer LANGUAGES CXX)

find_package(imtg CONFIG REQUIRED)
if(NOT TARGET imtg::telegram_transport)
    message(FATAL_ERROR "Missing installed imtg::telegram_transport target")
endif()

add_executable(imtg_telegram_transport_consumer main.cpp)
target_link_libraries(imtg_telegram_transport_consumer PRIVATE imtg::telegram_transport)
enable_testing()
add_test(NAME imtg_telegram_transport_consumer COMMAND imtg_telegram_transport_consumer)
