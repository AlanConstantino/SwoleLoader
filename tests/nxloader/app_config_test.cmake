# Run with: cmake -P tests/nxloader/app_config_test.cmake
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/app_config.cmake)

function(expect_repo input expected)
    app_github_repo("${input}" actual)
    if (NOT actual STREQUAL expected)
        message(FATAL_ERROR "'${input}' gave '${actual}', expected '${expected}'")
    endif()
endfunction()

expect_repo("" "")
expect_repo("   " "")
expect_repo("https://github.com/owner/repo" "owner/repo")
expect_repo("https://github.com/owner/repo/" "owner/repo")
expect_repo("https://github.com/owner/repo.git" "owner/repo")
expect_repo("HTTPS://WWW.GitHub.com/Owner-1/My.Repo_2" "Owner-1/My.Repo_2")
expect_repo("http://github.com/owner/repo" "owner/repo")
expect_repo("github.com/owner/repo" "owner/repo")
expect_repo("owner/repo" "owner/repo")
expect_repo(" owner/repo " "owner/repo")

# Each rejected value runs in a child process because FATAL_ERROR ends this one.
foreach(bad
    "https://gitlab.com/owner/repo"
    "https://github.com/owner"
    "https://github.com/owner/repo/releases"
    "https://github.com/-owner/repo"
    "owner/repo name"
    "owner/.."
    "owner/."
    "https://github.com/owner/repo?tab=readme"
    "https://evil.example/github.com/owner/repo")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DINPUT=${bad} -P ${CMAKE_CURRENT_LIST_DIR}/app_config_reject.cmake
        RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
    if (result EQUAL 0)
        message(FATAL_ERROR "'${bad}' was accepted")
    endif()
endforeach()

function(expect_config valid)
    execute_process(
        COMMAND ${CMAKE_COMMAND} ${ARGN} -P ${CMAKE_CURRENT_LIST_DIR}/app_config_reject.cmake
        RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
    if (valid AND NOT result EQUAL 0)
        message(FATAL_ERROR "valid config rejected (${ARGN}): ${error}")
    elseif (NOT valid AND result EQUAL 0)
        message(FATAL_ERROR "invalid config accepted: ${ARGN}")
    endif()
endfunction()

set(good -DAPP_NAME=Name -DAPP_AUTHOR=Author -DAPP_VERSION=0.1.6 -DAPP_FOLDER=app -DAPP_NRO=app.nro)
expect_config(TRUE ${good})
expect_config(TRUE ${good} -DAPP_VERSION=255.255.255)
expect_config(TRUE ${good} "-DAPP_NAME=My App 2")
expect_config(FALSE ${good} -DAPP_VERSION=0.1)
expect_config(FALSE ${good} -DAPP_VERSION=v0.1.6)
expect_config(FALSE ${good} -DAPP_VERSION=0.1.256)
expect_config(FALSE ${good} -DAPP_VERSION=0.01.6)
expect_config(FALSE ${good} "-DAPP_NAME=Bad\"Name")
expect_config(FALSE ${good} "-DAPP_AUTHOR=Bad\\Name")
expect_config(FALSE ${good} -DAPP_FOLDER=a/b)
expect_config(FALSE ${good} -DAPP_FOLDER=..)
expect_config(FALSE ${good} -DAPP_NRO=app.elf)
expect_config(FALSE ${good} -DAPP_NRO=dir/app.nro)

message(STATUS "App config tests passed: repository links, versions, names and paths.")
