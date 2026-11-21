# The application owns shared dependency configuration. An existing target
# wins before any bundled path is examined or provider option is defaulted.
if(NOT TARGET log-it-cpp::log-it-cpp)
    set(IMTG_LOGIT_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/external/log-it-cpp" CACHE PATH
        "Pinned LogIt++ source used when no target is provided")
    if(NOT EXISTS "${IMTG_LOGIT_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "imtg requires log-it-cpp::log-it-cpp; provide that target or initialize external/log-it-cpp")
    endif()
    if(NOT TARGET time_shield::time_shield)
        set(IMTG_TIME_SHIELD_SOURCE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/external/time-shield-cpp" CACHE PATH
            "Pinned TimeShield source used when no target is provided")
        if(NOT EXISTS "${IMTG_TIME_SHIELD_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "imtg requires TimeShield for LogIt++; provide time_shield::time_shield or initialize external/time-shield-cpp")
        endif()
        if(NOT DEFINED TIME_SHIELD_ENABLE_NTP_CLIENT)
            set(TIME_SHIELD_ENABLE_NTP_CLIENT OFF CACHE BOOL "Enable TimeShield NTP client")
        endif()
        if(NOT DEFINED TIME_SHIELD_CPP_BUILD_TESTS)
            set(TIME_SHIELD_CPP_BUILD_TESTS OFF CACHE BOOL "Build TimeShield tests")
        endif()
        if(NOT DEFINED TIME_SHIELD_CPP_BUILD_EXAMPLES)
            set(TIME_SHIELD_CPP_BUILD_EXAMPLES OFF CACHE BOOL "Build TimeShield examples")
        endif()
        add_subdirectory("${IMTG_TIME_SHIELD_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/_deps/time-shield")
        install(FILES "${IMTG_TIME_SHIELD_SOURCE_DIR}/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/time-shield-cpp")
    endif()
    if(NOT DEFINED LOGIT_CPP_BUILD_TESTS)
        set(LOGIT_CPP_BUILD_TESTS OFF CACHE BOOL "Build LogIt++ tests")
    endif()
    if(NOT DEFINED LOGIT_CPP_BUILD_EXAMPLES)
        set(LOGIT_CPP_BUILD_EXAMPLES OFF CACHE BOOL "Build LogIt++ examples")
    endif()
    # The provider defaults to lightweight backends; preserve every explicit
    # LOGIT_WITH_* choice, including ABI-affecting context support.
    add_subdirectory("${IMTG_LOGIT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/_deps/log-it-cpp")
    install(FILES "${IMTG_LOGIT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/log-it-cpp")
endif()
if(NOT TARGET log-it-cpp::log-it-cpp)
    message(FATAL_ERROR "imtg could not resolve required log-it-cpp::log-it-cpp")
endif()
