# Validation for the settings in app.cmake.

# Reduces a GitHub repository link to "owner/repo". Accepts
# https://github.com/owner/repo, github.com/owner/repo and owner/repo, with an
# optional .git suffix or trailing slash. An empty input gives an empty result.
function(app_github_repo input out_var)
    string(STRIP "${input}" repo)
    if (repo STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    string(REGEX REPLACE "^[Hh][Tt][Tt][Pp][Ss]?://" "" repo "${repo}")
    string(REGEX REPLACE "^([Ww][Ww][Ww]\\.)?[Gg][Ii][Tt][Hh][Uu][Bb]\\.[Cc][Oo][Mm]/" "" repo "${repo}")
    string(REGEX REPLACE "/+$" "" repo "${repo}")
    string(REGEX REPLACE "\\.git$" "" repo "${repo}")

    if (NOT repo MATCHES "^[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?/[A-Za-z0-9._-]+$"
        OR repo MATCHES "/\\.\\.?$")
        message(FATAL_ERROR "APP_UPDATE_REPO is not a GitHub repository link: '${input}'. "
            "Use https://github.com/owner/repo.")
    endif()
    set(${out_var} "${repo}" PARENT_SCOPE)
endfunction()

function(app_check_config)
    # CMake regexes have no {n} repetition, so the three parts are spelled out.
    set(part "(0|[1-9][0-9]?|1[0-9][0-9]|2[0-4][0-9]|25[0-5])")
    if (NOT APP_VERSION MATCHES "^${part}\\.${part}\\.${part}$")
        message(FATAL_ERROR "APP_VERSION must be MAJOR.MINOR.PATCH with parts from 0 to 255: '${APP_VERSION}'.")
    endif()
    if (APP_NAME STREQUAL "" OR APP_AUTHOR STREQUAL "")
        message(FATAL_ERROR "APP_NAME and APP_AUTHOR must not be empty.")
    endif()
    if (APP_NAME MATCHES "[\"\\\\]" OR APP_AUTHOR MATCHES "[\"\\\\]")
        message(FATAL_ERROR "APP_NAME and APP_AUTHOR must not contain quotes or backslashes.")
    endif()
    if (NOT APP_FOLDER MATCHES "^[A-Za-z0-9._-]+$" OR APP_FOLDER MATCHES "^\\.")
        message(FATAL_ERROR "APP_FOLDER must be a single folder name: '${APP_FOLDER}'.")
    endif()
    if (NOT APP_NRO MATCHES "^[A-Za-z0-9._-]+\\.nro$" OR APP_NRO MATCHES "^\\.")
        message(FATAL_ERROR "APP_NRO must be a file name ending in .nro: '${APP_NRO}'.")
    endif()
endfunction()
