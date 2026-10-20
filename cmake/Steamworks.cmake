# KettleLAN – wyszukiwanie Steamworks SDK.
#
# SDK jest potrzebne tylko do KOMPILACJI. Licencja Valve nie pozwala trzymać go w repozytorium,
# dlatego trzeba je pobrać samemu (za darmo, po zalogowaniu kontem Steam):
#   https://partner.steamgames.com/downloads/list
#
# Kolejność szukania:
#   1. -DSTEAMWORKS_SDK_DIR=<folder z podfolderem public/steam>
#   2. -DSTEAMWORKS_SDK_ZIP=<plik steamworks_sdk_XXX.zip>
#   3. zmienna środowiskowa STEAMWORKS_SDK_URL (używana przez GitHub Actions)
#   4. najnowszy steamworks_sdk_*.zip w ~/Downloads, ~/Pobrane lub w folderze projektu

set(STEAMWORKS_SDK_DIR "" CACHE PATH "Folder rozpakowanego Steamworks SDK")
set(STEAMWORKS_SDK_ZIP "" CACHE FILEPATH "Plik steamworks_sdk_XXX.zip")

set(_sw_extract "${CMAKE_BINARY_DIR}/_deps/steamworks")

function(_sw_is_sdk dir out)
    if(EXISTS "${dir}/public/steam/steam_api.h")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(_sw_extract_zip zip)
    message(STATUS "Steamworks SDK: rozpakowuję ${zip}")
    file(REMOVE_RECURSE "${_sw_extract}")
    file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${_sw_extract}")
endfunction()

set(_sw_root "")

if(STEAMWORKS_SDK_DIR)
    _sw_is_sdk("${STEAMWORKS_SDK_DIR}" _ok)
    if(NOT _ok)
        message(FATAL_ERROR "STEAMWORKS_SDK_DIR=${STEAMWORKS_SDK_DIR} nie zawiera public/steam/steam_api.h")
    endif()
    set(_sw_root "${STEAMWORKS_SDK_DIR}")
endif()

if(NOT _sw_root)
    foreach(_cand "${_sw_extract}/sdk" "${_sw_extract}")
        _sw_is_sdk("${_cand}" _ok)
        if(_ok AND NOT STEAMWORKS_SDK_ZIP)
            set(_sw_root "${_cand}")
            break()
        endif()
    endforeach()
endif()

if(NOT _sw_root)
    set(_sw_zip "${STEAMWORKS_SDK_ZIP}")
    if(NOT _sw_zip AND DEFINED ENV{STEAMWORKS_SDK_URL} AND NOT "$ENV{STEAMWORKS_SDK_URL}" STREQUAL "")
        set(_sw_zip "${CMAKE_BINARY_DIR}/_deps/steamworks_sdk.zip")
        message(STATUS "Steamworks SDK: pobieram z adresu podanego w STEAMWORKS_SDK_URL")
        file(DOWNLOAD "$ENV{STEAMWORKS_SDK_URL}" "${_sw_zip}" STATUS _st)
        list(GET _st 0 _code)
        if(NOT _code EQUAL 0)
            message(FATAL_ERROR "Nie udało się pobrać SDK: ${_st}")
        endif()
    endif()
    if(NOT _sw_zip)
        set(_dirs "${CMAKE_SOURCE_DIR}")
        if(DEFINED ENV{XDG_DOWNLOAD_DIR})
            list(APPEND _dirs "$ENV{XDG_DOWNLOAD_DIR}")
        endif()
        if(DEFINED ENV{HOME})
            list(APPEND _dirs "$ENV{HOME}/Downloads" "$ENV{HOME}/Pobrane")
        endif()
        if(DEFINED ENV{USERPROFILE})
            list(APPEND _dirs "$ENV{USERPROFILE}/Downloads")
        endif()
        set(_found "")
        foreach(_d ${_dirs})
            file(GLOB _z "${_d}/steamworks_sdk_*.zip")
            list(APPEND _found ${_z})
        endforeach()
        if(_found)
            list(SORT _found COMPARE NATURAL)
            list(GET _found -1 _sw_zip)
        endif()
    endif()
    if(_sw_zip)
        _sw_extract_zip("${_sw_zip}")
        foreach(_cand "${_sw_extract}/sdk" "${_sw_extract}")
            _sw_is_sdk("${_cand}" _ok)
            if(_ok)
                set(_sw_root "${_cand}")
                break()
            endif()
        endforeach()
        if(NOT _sw_root)
            message(FATAL_ERROR "Plik ${_sw_zip} nie wygląda na Steamworks SDK.")
        endif()
    endif()
endif()

if(NOT _sw_root)
    message(FATAL_ERROR
        "\nNie znaleziono Steamworks SDK – jest potrzebne do kompilacji (gotowe paczki go nie wymagają).\n"
        "  1. Wejdź na https://partner.steamgames.com/downloads/list i zaloguj się kontem Steam.\n"
        "  2. Pobierz najnowsze „Steamworks SDK” (plik steamworks_sdk_XXX.zip) do folderu Pobrane.\n"
        "  3. Uruchom budowanie ponownie – plik zostanie znaleziony automatycznie.\n"
        "Najprościej: ./scripts/build.sh (przeprowadzi Cię przez to krok po kroku).\n")
endif()

message(STATUS "Steamworks SDK: ${_sw_root}")

if(WIN32)
    set(STEAMWORKS_REDIST "${_sw_root}/redistributable_bin/win64/steam_api64.dll")
    set(_sw_implib "${_sw_root}/redistributable_bin/win64/steam_api64.lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(STEAMWORKS_REDIST "${_sw_root}/redistributable_bin/linux64/libsteam_api.so")
else()
    message(FATAL_ERROR "Steamworks: nieobsługiwany system ${CMAKE_SYSTEM_NAME}")
endif()

if(NOT EXISTS "${STEAMWORKS_REDIST}")
    message(FATAL_ERROR "Brak biblioteki ${STEAMWORKS_REDIST} w SDK.")
endif()

add_library(Steamworks::api SHARED IMPORTED)
set_target_properties(Steamworks::api PROPERTIES
    IMPORTED_LOCATION "${STEAMWORKS_REDIST}"
    INTERFACE_INCLUDE_DIRECTORIES "${_sw_root}/public/steam"
)
if(WIN32)
    set_target_properties(Steamworks::api PROPERTIES IMPORTED_IMPLIB "${_sw_implib}")
endif()
