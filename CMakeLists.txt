cmake_minimum_required(VERSION 3.20)
project(KettleLAN VERSION 0.2.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(KETTLELAN_BUILD_APP "Build the KettleLAN program (needs the Steamworks SDK)" ON)
option(KETTLELAN_BUILD_TESTS "Build the core tests (no Steamworks SDK needed)" ON)

option(KETTLELAN_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (tests)" OFF)
option(KETTLELAN_SECURITY_TESTS "Add attack-simulating commands to kettlelan-cli (testing only)" OFF)

if(MSVC)
    set(KETTLELAN_WARNINGS /W4 /utf-8)
else()
    set(KETTLELAN_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Hardening: stack protection, buffer overflow checks, randomised addresses (PIE),
# read-only relocations (RELRO) and a non-executable stack.
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

# ---------------------------------------------------------------- core (no dependencies)
add_library(kettle_core STATIC
    src/core/filter.cpp
    src/core/ip.cpp
    src/core/messages.cpp
    src/core/netkeys.cpp
    src/core/random.cpp
    third_party/monocypher/monocypher.c
    src/core/netcode.cpp
    src/core/protocol.cpp
    src/core/router.cpp
    src/core/siphash.cpp
    src/core/text.cpp
)
target_include_directories(kettle_core PUBLIC src/core PRIVATE third_party/monocypher)
# our own warnings for our code; Monocypher (third_party, C) is built as it is
target_compile_options(kettle_core PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${KETTLELAN_WARNINGS}>")
if(WIN32)
    target_link_libraries(kettle_core PUBLIC bcrypt) # BCryptGenRandom
endif()

if(KETTLELAN_BUILD_TESTS)
    enable_testing()
    add_executable(kettlelan_tests tests/test_core.cpp)
    target_link_libraries(kettlelan_tests PRIVATE kettle_core)
    target_include_directories(kettlelan_tests PRIVATE third_party/monocypher) # RFC 9106 test vector
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
option(KETTLELAN_BUILD_GUI "Build the graphical version (Qt 6)" ON)

if(KETTLELAN_BUILD_APP)
    include(cmake/Steamworks.cmake)
    find_package(Threads REQUIRED)

    # Shared part: Steam + network adapter (used by the GUI and the command-line version).
    add_library(kettle_net STATIC
        src/steam/session.cpp
        src/steam/network.cpp
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(kettle_net PRIVATE src/platform/linux/tun_linux.cpp)
    elseif(WIN32)
        include(cmake/Wintun.cmake)
        target_sources(kettle_net PRIVATE src/platform/windows/tun_windows.cpp)
        target_include_directories(kettle_net PRIVATE third_party/wintun)
        target_compile_definitions(kettle_net PUBLIC NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
        target_link_libraries(kettle_net PUBLIC iphlpapi ws2_32 ole32 oleaut32 advapi32 wintrust crypt32)
    else()
        message(FATAL_ERROR "Unsupported system: ${CMAKE_SYSTEM_NAME}")
    endif()
    target_include_directories(kettle_net PUBLIC src/platform src/steam)
    if(KETTLELAN_SECURITY_TESTS)
        target_compile_definitions(kettle_net PUBLIC KETTLELAN_SECURITY_TESTS)
    endif()
    target_compile_options(kettle_net PRIVATE ${KETTLELAN_WARNINGS})
    target_link_libraries(kettle_net PUBLIC kettle_core Steamworks::api Threads::Threads)

    # Command-line version – useful for diagnostics.
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
            src/gui/qml/NetworkDialog.qml
            src/gui/qml/Toast.qml
            src/gui/qml/StatusPage.qml
            src/gui/qml/StartPage.qml
            src/gui/qml/NetworkPage.qml
            src/gui/qml/SettingsPage.qml
        )
        # QML files end up at qrc:/qt/qml/KettleLAN/<name>.qml
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

    if(WIN32)
        # Windows resources (icon, version) and administrator rights (Wintun needs them).
        set(_rc "${CMAKE_SOURCE_DIR}/packaging/windows/kettlelan.rc")
        set_source_files_properties("${_rc}" PROPERTIES COMPILE_DEFINITIONS
            "KETTLELAN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};KETTLELAN_VERSION_MINOR=${PROJECT_VERSION_MINOR};KETTLELAN_VERSION_PATCH=${PROJECT_VERSION_PATCH};KETTLELAN_VERSION_STRING=\"${PROJECT_VERSION}\"")
        foreach(t ${KETTLELAN_TARGETS})
            target_sources(${t} PRIVATE "${_rc}")
            if(MSVC)
                target_link_options(${t} PRIVATE "/MANIFESTUAC:level='requireAdministrator'")
            endif()
            add_custom_command(TARGET ${t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WINTUN_DLL}" "$<TARGET_FILE_DIR:${t}>")
        endforeach()
        if(TARGET kettlelan)
            set_target_properties(kettlelan PROPERTIES OUTPUT_NAME KettleLAN)
        endif()
    endif()

    foreach(t ${KETTLELAN_TARGETS})
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            # libsteam_api.so sits next to the program
            set_target_properties(${t} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
        endif()
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${STEAMWORKS_REDIST}" "$<TARGET_FILE_DIR:${t}>")
        if(UNIX)
            add_custom_command(TARGET ${t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/scripts/kettlelan-helper" "$<TARGET_FILE_DIR:${t}>")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------- installation and packages
#   .deb:      cmake -B build -DKETTLELAN_PACKAGE=deb -DKETTLELAN_DEB_DISTRO=debian12 && cpack --config build/CPackConfig.cmake
#   AppImage:  packaging/linux/build-appimage.sh (AppImage layout: -DKETTLELAN_PACKAGE=appimage)
set(KETTLELAN_PACKAGE "" CACHE STRING "Install layout: deb, appimage or empty (no install rules)")

if(KETTLELAN_BUILD_APP AND KETTLELAN_BUILD_GUI AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND KETTLELAN_PACKAGE)
    set(KETTLELAN_ICON_SIZES 16 24 32 48 64 128 256 512)
    foreach(sz ${KETTLELAN_ICON_SIZES})
        install(FILES packaging/linux/icons/kettlelan-${sz}.png
                DESTINATION share/icons/hicolor/${sz}x${sz}/apps RENAME kettlelan.png)
    endforeach()
    install(FILES packaging/linux/kettlelan.desktop DESTINATION share/applications)

    if(KETTLELAN_PACKAGE STREQUAL "deb")
        # Programs, the Steam library and the helper in /usr/lib/kettlelan (owned by root).
        install(TARGETS kettlelan kettlelan-cli RUNTIME DESTINATION lib/kettlelan)
        install(FILES "${STEAMWORKS_REDIST}" DESTINATION lib/kettlelan)
        install(PROGRAMS scripts/kettlelan-helper DESTINATION lib/kettlelan)
        install(PROGRAMS packaging/linux/kettlelan.sh DESTINATION bin RENAME kettlelan)
        install(PROGRAMS packaging/linux/kettlelan-cli.sh DESTINATION bin RENAME kettlelan-cli)
        install(FILES packaging/linux/io.github.maxiuz94.kettlelan.policy DESTINATION share/polkit-1/actions)
        install(FILES LICENSE DESTINATION share/doc/kettlelan RENAME copyright)
        install(FILES README.md SECURITY.md THIRD-PARTY-NOTICES.md DESTINATION share/doc/kettlelan)

        set(KETTLELAN_DEB_DISTRO "" CACHE STRING "Suffix for the .deb file name, e.g. debian12 or ubuntu24.04")
        set(CPACK_GENERATOR DEB)
        set(CPACK_PACKAGE_NAME kettlelan)
        set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
        set(CPACK_PACKAGE_VENDOR "KettleLAN")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-source virtual LAN over the Steam network")
        set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "maxiuz94 <maxiuz94@users.noreply.github.com>")
        set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/maxiuz94/kettlelan")
        set(CPACK_DEBIAN_PACKAGE_SECTION net)
        set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
        set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Play classic LAN games (Quake 3, Unreal Tournament, Counter-Strike 1.6,
Warcraft III...) with friends as if you were on the same router.
No player limit, no port forwarding and no own server - traffic goes
through the encrypted Steam network.")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "pkexec | policykit-1, iproute2, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-shapes, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-workerscript")
        set(CPACK_DEBIAN_PACKAGE_SUGGESTS "steam-installer | steam-launcher | steam")
        if(KETTLELAN_DEB_DISTRO)
            set(CPACK_DEBIAN_FILE_NAME "kettlelan_${PROJECT_VERSION}_${KETTLELAN_DEB_DISTRO}_amd64.deb")
        else()
            set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        endif()
        include(CPack)
    elseif(KETTLELAN_PACKAGE STREQUAL "appimage")
        # AppImage: everything in usr/bin, linuxdeploy adds the Qt libraries.
        install(TARGETS kettlelan kettlelan-cli RUNTIME DESTINATION bin)
        install(FILES "${STEAMWORKS_REDIST}" DESTINATION bin)
        install(PROGRAMS scripts/kettlelan-helper DESTINATION bin)
    else()
        message(FATAL_ERROR "KETTLELAN_PACKAGE must be \"deb\" or \"appimage\".")
    endif()
endif()
