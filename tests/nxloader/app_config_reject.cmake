# Helper for app_config_test.cmake: validates one value and fails like a build would.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/app_config.cmake)
if (DEFINED INPUT)
    app_github_repo("${INPUT}" repo)
else()
    app_check_config()
endif()
