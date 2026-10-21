# KettleLAN – locating the Steamworks SDK.
#
# The SDK is only needed to BUILD the program. Valve's license does not allow keeping it in the
# repository, so you need to download it yourself (free, after signing in with a Steam account):
#   https://partner.steamgames.com/downloads/list
#
# Search order:
#   1. -DSTEAMWORKS_SDK_DIR=<folder with a public/steam subfolder>
#   2. -DSTEAMWORKS_SDK_ZIP=<steamworks_sdk_XXX.zip file>
#   3. STEAMWORKS_SDK_URL environment variable
#   4. the newest steamworks_sdk_*.zip in ~/Downloads, ~/Pobrane or the project folder

set(STEAMWORKS_SDK_DIR "" CACHE PATH "Folder of the extracted Steamworks SDK")
set(STEAMWORKS_SDK_ZIP "" CACHE FILEPATH "steamworks_sdk_XXX.zip file")

set(_sw_extract "${CMAKE_BINARY_DIR}/_deps/steamworks")

function(_sw_is_sdk dir out)
    if(EXISTS "${dir}/public/steam/steam_api.h")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(_sw_extract_zip zip)
    message(STATUS "Steamworks SDK: extracting ${zip}")
    file(REMOVE_RECURSE "${_sw_extract}")
    file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${_sw_extract}")
endfunction()

set(_sw_root "")

if(STEAMWORKS_SDK_DIR)
    _sw_is_sdk("${STEAMWORKS_SDK_DIR}" _ok)
    if(NOT _ok)
        message(FATAL_ERROR "STEAMWORKS_SDK_DIR=${STEAMWORKS_SDK_DIR} does not contain public/steam/steam_api.h")
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
        message(STATUS "Steamworks SDK: downloading from STEAMWORKS_SDK_URL")
        file(DOWNLOAD "$ENV{STEAMWORKS_SDK_URL}" "${_sw_zip}" STATUS _st)
        list(GET _st 0 _code)
        if(NOT _code EQUAL 0)
            message(FATAL_ERROR "Could not download the SDK: ${_st}")
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
            message(FATAL_ERROR "${_sw_zip} does not look like the Steamworks SDK.")
        endif()
    endif()
endif()

if(NOT _sw_root)
    message(FATAL_ERROR
        "\nSteamworks SDK not found – it is needed to build KettleLAN (ready-made packages do not need it).\n"
        "  1. Go to https://partner.steamgames.com/downloads/list and sign in with your Steam account.\n"
        "  2. Download the latest \"Steamworks SDK\" (steamworks_sdk_XXX.zip) to your Downloads folder.\n"
        "  3. Run the build again – the file will be found automatically.\n"
        "Easiest: ./scripts/build.sh (guides you step by step).\n")
endif()

message(STATUS "Steamworks SDK: ${_sw_root}")

if(WIN32)
    set(STEAMWORKS_REDIST "${_sw_root}/redistributable_bin/win64/steam_api64.dll")
    set(_sw_implib "${_sw_root}/redistributable_bin/win64/steam_api64.lib")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(STEAMWORKS_REDIST "${_sw_root}/redistributable_bin/linux64/libsteam_api.so")
else()
    message(FATAL_ERROR "Steamworks: unsupported system ${CMAKE_SYSTEM_NAME}")
endif()

if(NOT EXISTS "${STEAMWORKS_REDIST}")
    message(FATAL_ERROR "${STEAMWORKS_REDIST} is missing from the SDK.")
endif()

add_library(Steamworks::api SHARED IMPORTED)
set_target_properties(Steamworks::api PROPERTIES
    IMPORTED_LOCATION "${STEAMWORKS_REDIST}"
    INTERFACE_INCLUDE_DIRECTORIES "${_sw_root}/public/steam"
)
if(WIN32)
    set_target_properties(Steamworks::api PROPERTIES IMPORTED_IMPLIB "${_sw_implib}")
endif()
