cmake_minimum_required(VERSION 3.20)
project(KettleLAN VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Typ budowania" FORCE)
endif()

option(KETTLELAN_BUILD_APP "Buduj program KettleLAN (wymaga Steamworks SDK)" ON)
option(KETTLELAN_BUILD_TESTS "Buduj testy rdzenia (bez Steamworks SDK)" ON)

if(MSVC)
    set(KETTLELAN_WARNINGS /W4 /utf-8)
else()
    set(KETTLELAN_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- rdzeń (bez zależności)
add_library(kettle_core STATIC
    src/core/ip.cpp
    src/core/netcode.cpp
    src/core/protocol.cpp
    src/core/siphash.cpp
)
target_include_directories(kettle_core PUBLIC src/core)
target_compile_options(kettle_core PRIVATE ${KETTLELAN_WARNINGS})

if(KETTLELAN_BUILD_TESTS)
    enable_testing()
    add_executable(kettlelan_tests tests/test_core.cpp)
    target_link_libraries(kettlelan_tests PRIVATE kettle_core)
    target_compile_options(kettlelan_tests PRIVATE ${KETTLELAN_WARNINGS})
    add_test(NAME core COMMAND kettlelan_tests)
endif()

# ---------------------------------------------------------------- program
if(KETTLELAN_BUILD_APP)
    include(cmake/Steamworks.cmake)
    find_package(Threads REQUIRED)

    add_executable(kettlelan
        src/app/main_cli.cpp
        src/steam/session.cpp
        src/steam/network.cpp
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(kettlelan PRIVATE src/platform/linux/tun_linux.cpp)
        # libsteam_api.so leży obok programu
        set_target_properties(kettlelan PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    elseif(WIN32)
        message(FATAL_ERROR "Wersja dla Windowsa powstaje w etapie 2.")
    else()
        message(FATAL_ERROR "Nieobsługiwany system: ${CMAKE_SYSTEM_NAME}")
    endif()

    target_include_directories(kettlelan PRIVATE src/platform src/steam)
    target_compile_definitions(kettlelan PRIVATE KETTLELAN_VERSION="${PROJECT_VERSION}")
    target_compile_options(kettlelan PRIVATE ${KETTLELAN_WARNINGS})
    target_link_libraries(kettlelan PRIVATE kettle_core Steamworks::api Threads::Threads)

    add_custom_command(TARGET kettlelan POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAMWORKS_REDIST}" "$<TARGET_FILE_DIR:kettlelan>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/scripts/kettlelan-helper" "$<TARGET_FILE_DIR:kettlelan>"
        COMMENT "Kopiuję bibliotekę Steama i pomocnika obok programu"
    )
endif()
