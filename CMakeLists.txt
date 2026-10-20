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

option(KETTLELAN_SANITIZE "Buduj z AddressSanitizer i UndefinedBehaviorSanitizer (testy)" OFF)
option(KETTLELAN_SECURITY_TESTS "Dodaj do kettlelan-cli polecenia symulujące ataki (tylko do testów)" OFF)

if(MSVC)
    set(KETTLELAN_WARNINGS /W4 /utf-8)
else()
    set(KETTLELAN_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Utwardzenie programu: ochrona stosu, kontrola przepełnień buforów, adresy losowe (PIE),
# tablice adresów tylko do odczytu (RELRO) i niewykonywalny stos.
if(NOT MSVC AND NOT KETTLELAN_SANITIZE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    include(CheckPIESupported)
    check_pie_supported()
    add_compile_options(-fstack-protector-strong -fstack-clash-protection)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        add_compile_options(-fcf-protection)
    endif()
    add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2> _GLIBCXX_ASSERTIONS)
    add_link_options(-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
endif()

if(KETTLELAN_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------- rdzeń (bez zależności)
add_library(kettle_core STATIC
    src/core/filter.cpp
    src/core/ip.cpp
    src/core/netcode.cpp
    src/core/protocol.cpp
    src/core/router.cpp
    src/core/siphash.cpp
    src/core/text.cpp
)
target_include_directories(kettle_core PUBLIC src/core)
target_compile_options(kettle_core PRIVATE ${KETTLELAN_WARNINGS})

if(KETTLELAN_BUILD_TESTS)
    enable_testing()
    add_executable(kettlelan_tests tests/test_core.cpp)
    target_link_libraries(kettlelan_tests PRIVATE kettle_core)
    target_compile_options(kettlelan_tests PRIVATE ${KETTLELAN_WARNINGS})
    add_test(NAME core COMMAND kettlelan_tests)

    add_executable(kettlelan_security_tests tests/test_security.cpp)
    target_link_libraries(kettlelan_security_tests PRIVATE kettle_core)
    target_compile_options(kettlelan_security_tests PRIVATE ${KETTLELAN_WARNINGS})
    add_test(NAME security COMMAND kettlelan_security_tests)

    add_executable(kettlelan_fuzz tests/fuzz_core.cpp)
    target_link_libraries(kettlelan_fuzz PRIVATE kettle_core)
    add_test(NAME fuzz COMMAND kettlelan_fuzz 300000)

    if(UNIX)
        add_test(NAME helper COMMAND sh "${CMAKE_SOURCE_DIR}/tests/test_helper.sh" "${CMAKE_SOURCE_DIR}/scripts/kettlelan-helper")
    endif()
endif()

# ---------------------------------------------------------------- program
option(KETTLELAN_BUILD_GUI "Buduj wersję z interfejsem graficznym (Qt 6)" ON)

if(KETTLELAN_BUILD_APP)
    include(cmake/Steamworks.cmake)
    find_package(Threads REQUIRED)

    # Wspólna część: Steam + karta sieciowa (używana przez GUI i wersję konsolową).
    add_library(kettle_net STATIC
        src/steam/session.cpp
        src/steam/network.cpp
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(kettle_net PRIVATE src/platform/linux/tun_linux.cpp)
    elseif(WIN32)
        message(FATAL_ERROR "Wersja dla Windowsa powstaje w etapie 2.")
    else()
        message(FATAL_ERROR "Nieobsługiwany system: ${CMAKE_SYSTEM_NAME}")
    endif()
    target_include_directories(kettle_net PUBLIC src/platform src/steam)
    if(KETTLELAN_SECURITY_TESTS)
        target_compile_definitions(kettle_net PUBLIC KETTLELAN_SECURITY_TESTS)
    endif()
    target_compile_options(kettle_net PRIVATE ${KETTLELAN_WARNINGS})
    target_link_libraries(kettle_net PUBLIC kettle_core Steamworks::api Threads::Threads)

    # Wersja konsolowa – przydatna do diagnostyki.
    add_executable(kettlelan-cli src/app/main_cli.cpp)
    target_compile_definitions(kettlelan-cli PRIVATE KETTLELAN_VERSION="${PROJECT_VERSION}")
    target_compile_options(kettlelan-cli PRIVATE ${KETTLELAN_WARNINGS})
    target_link_libraries(kettlelan-cli PRIVATE kettle_net)
    set(KETTLELAN_TARGETS kettlelan-cli)

    if(KETTLELAN_BUILD_GUI)
        find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2 Widgets Network)
        qt_standard_project_setup()

        qt_add_executable(kettlelan
            src/gui/main_gui.cpp
            src/gui/backend.cpp
            src/gui/engine.cpp
            src/gui/util.cpp
        )
        set(KETTLELAN_QML
            src/gui/qml/Main.qml
            src/gui/qml/Theme.qml
            src/gui/qml/SvgIcon.qml
            src/gui/qml/AppButton.qml
            src/gui/qml/IconButton.qml
            src/gui/qml/InfoCard.qml
            src/gui/qml/Toggle.qml
            src/gui/qml/Toast.qml
            src/gui/qml/StatusPage.qml
            src/gui/qml/StartPage.qml
            src/gui/qml/NetworkPage.qml
            src/gui/qml/SettingsPage.qml
        )
        # pliki QML trafiają do qrc:/qt/qml/KettleLAN/<nazwa>.qml
        foreach(f ${KETTLELAN_QML})
            get_filename_component(n "${f}" NAME)
            set_source_files_properties("${f}" PROPERTIES QT_RESOURCE_ALIAS "${n}")
        endforeach()
        set_source_files_properties(src/gui/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
        qt_add_qml_module(kettlelan
            URI KettleLAN
            VERSION 1.0
            RESOURCE_PREFIX /qt/qml
            QML_FILES ${KETTLELAN_QML}
        )
        target_include_directories(kettlelan PRIVATE src/gui)
        target_compile_definitions(kettlelan PRIVATE KETTLELAN_VERSION="${PROJECT_VERSION}")
        target_compile_options(kettlelan PRIVATE ${KETTLELAN_WARNINGS})
        target_link_libraries(kettlelan PRIVATE kettle_net Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Network)
        set_target_properties(kettlelan PROPERTIES WIN32_EXECUTABLE ON)
        list(APPEND KETTLELAN_TARGETS kettlelan)
    endif()

    foreach(t ${KETTLELAN_TARGETS})
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # libsteam_api.so leży obok programu
            set_target_properties(${t} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
        endif()
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAMWORKS_REDIST}" "$<TARGET_FILE_DIR:${t}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/scripts/kettlelan-helper" "$<TARGET_FILE_DIR:${t}>"
        )
    endforeach()
endif()
