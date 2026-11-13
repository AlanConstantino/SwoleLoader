cmake_minimum_required(VERSION 3.13)

# usb drive formats. FAT32 and exFAT are always supported.
# ntfs is provided by ntfs-3g (GPLv2+, compatible with sphaira's GPLv3).
# needs the portlib: (dkp-)pacman -S switch-ntfs-3g
option(ENABLE_NTFS "enables NTFS hdd mounting, requires switch-ntfs-3g" ON)
# ext is provided by lwext4 (GPLv2). off by default, it adds a lot to the final
# binary size and next to no usb hdd is formatted with it.
# needs the portlib: (dkp-)pacman -S switch-lwext4
option(ENABLE_EXT4 "enables EXT2/3/4 hdd mounting, requires switch-lwext4" OFF)

include(${CMAKE_SOURCE_DIR}/app.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/app_config.cmake)
app_check_config()
app_github_repo("${APP_UPDATE_REPO}" APP_UPDATE_GITHUB_REPO)
if (APP_UPDATE_GITHUB_REPO)
    message(STATUS "updates: https://github.com/${APP_UPDATE_GITHUB_REPO}")
else()
    message(STATUS "updates: not configured (APP_UPDATE_REPO is empty)")
endif()

set(sphaira_VERSION ${APP_VERSION})

project(sphaira
    VERSION ${sphaira_VERSION}
    LANGUAGES C CXX
)

find_package(Git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" symbolic-ref --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_DIRTY
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (DEFINED sphaira_VERSION_OVERRIDE)
    set(sphaira_DISPLAY_VERSION "${sphaira_VERSION_OVERRIDE} [${GIT_COMMIT}]")
else()
    set(sphaira_DISPLAY_VERSION "${sphaira_VERSION} [${GIT_COMMIT}]")
endif()

add_executable(sphaira
    source/nxloader/browser.cpp
    source/nxloader/install.cpp
    source/nxloader/selection_size.cpp
    source/nxloader/selection_cart.cpp
    source/nxloader/package_size_fs.cpp
    source/nxloader/install_review.cpp
    source/nxloader/update_signature.cpp
    source/ui/menus/file_viewer.cpp
    source/ui/menus/image_viewer.cpp
    source/ui/menus/filebrowser.cpp
    source/ui/menus/file_picker.cpp
    source/ui/menus/main_menu.cpp
    source/ui/menus/menu_base.cpp

    source/ui/error_box.cpp
    source/ui/notification.cpp
    source/ui/nvg_util.cpp
    source/ui/option_box.cpp
    source/ui/popup_list.cpp
    source/ui/popup_multi_select.cpp
    source/ui/progress_box.cpp
    source/ui/scrollable_text.cpp
    source/ui/sidebar.cpp
    source/ui/widget.cpp
    source/ui/list.cpp
    source/ui/scrolling_text.cpp
    source/ui/forwarder_editor.cpp
    source/ui/steamgriddb_icon.cpp

    source/app.cpp
    source/download.cpp
    source/option.cpp
    source/evman.cpp
    source/fs.cpp
    source/image.cpp
    source/location.cpp
    source/log.cpp
    source/crash_handler.cpp
    source/main.cpp
    source/nro.cpp
    source/owo.cpp
    source/swkbd.cpp
    source/hasher.cpp
    source/i18n.cpp
    source/threaded_file_transfer.cpp
    source/title_info.cpp
    source/minizip_helper.cpp
    source/msp.cpp

    source/utils/utils.cpp
    source/utils/audio.cpp

    source/yati/yati.cpp
    source/yati/container/nsp.cpp
    source/yati/container/xci.cpp
    source/yati/source/file.cpp
    source/yati/source/stream.cpp
    source/yati/source/stream_file.cpp

    source/yati/nx/es.cpp
    source/yati/nx/keys.cpp
    source/yati/nx/nca.cpp
    source/yati/nx/ncz.cpp
    source/yati/nx/ncm.cpp
    source/yati/nx/ns.cpp

    source/yati/nx/nxdumptool_rsa.c
    source/yati/nx/nxdumptool/save.c
)

target_compile_definitions(sphaira PRIVATE
    -DAPP_VERSION="${sphaira_VERSION}"
    -DAPP_NAME="${APP_NAME}"
    -DAPP_NRO_NAME="${APP_NRO}"
    -DAPP_FOLDER="${APP_FOLDER}"
    -DAPP_CONFIG_DIR="/config/${APP_FOLDER}"
    -DAPP_CACHE_DIR="/config/${APP_FOLDER}/cache"
    -DAPP_UPDATE_REPO="${APP_UPDATE_GITHUB_REPO}"
    -DAPP_DISPLAY_VERSION="${sphaira_DISPLAY_VERSION}"
    -DCURL_NO_OLDIES=1
    -DDEV_BUILD=$<BOOL:${DEV_BUILD}>
    -DZSTD_STATIC_LINKING_ONLY=1
)

target_compile_options(sphaira PRIVATE
    -Wall
    -Wextra

    # unsure if it's a good idea to enable these by default as
    # it may cause breakage upon compiler updates.
    # -Werror
    # -Wfatal-errors

    # disabled as nx uses s64 for size and offset, however stl uses size_t instead, thus
    # there being a lot of warnings.
    -Wno-sign-compare
    # disabled as many overriden methods don't use the params.
    -Wno-unused-parameter
    # pedantic warning, missing fields are set to 0.
    -Wno-missing-field-initializers
    # disabled as it warns for strcat 2 paths together, but it will never
    # overflow due to fs enforcing a max path len anyway.
    -Wno-format-truncation
    # many false positives when LTO is not enabled.
    -Wno-suggest-final-types

    # the below are taken from my gba emulator, they've served me well ;)
    -Wformat-overflow=2
    -Wundef
    -Wmissing-include-dirs
    -fstrict-aliasing
    -Wstrict-overflow=2
    -Walloca
    -Wduplicated-cond
    -Wwrite-strings
    -Wdate-time
    -Wlogical-op
    -Wpacked
    -Wcast-qual
    -Wcast-align
    -Wimplicit-fallthrough=5
    -Wuninitialized
)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(libpulsar
    GIT_REPOSITORY https://github.com/ITotalJustice/switch-libpulsar.git
    GIT_TAG ac7bc97
)

FetchContent_Declare(nanovg
    GIT_REPOSITORY https://github.com/ITotalJustice/nanovg-deko3d.git
    GIT_TAG 2acb0f3
)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 5c20573
)

FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.12.0
)

FetchContent_Declare(minIni
    GIT_REPOSITORY https://github.com/ITotalJustice/minIni-nx.git
    GIT_TAG 6e952b6
)

FetchContent_Declare(zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.7
    SOURCE_SUBDIR build/cmake
)

FetchContent_Declare(libnxtc
    GIT_REPOSITORY https://github.com/DarkMatterCore/libnxtc.git
    GIT_TAG aa0a375
)

# USBHSFS_GPL turns on both ntfs *and* ext4, they can also be enabled
# individually, which is what we do here as ext4 is what costs the most
# binary size.
set(USBHSFS_GPL OFF)
set(USBHSFS_NTFS ${ENABLE_NTFS})
set(USBHSFS_EXT4 ${ENABLE_EXT4})
set(USBHSFS_SXOS_DISABLE ON)

if (ENABLE_NTFS)
    find_library(ntfs_3g_lib ntfs-3g)
    if (NOT ntfs_3g_lib)
        message(FATAL_ERROR "NTFS support is enabled but ntfs-3g is not installed, run '(dkp-)pacman -S switch-ntfs-3g' or configure with -DENABLE_NTFS=OFF")
    endif()
endif()

if (ENABLE_EXT4)
    find_library(lwext4_lib lwext4)
    if (NOT lwext4_lib)
        message(FATAL_ERROR "EXT4 support is enabled but lwext4 is not installed, run '(dkp-)pacman -S switch-lwext4' or configure with -DENABLE_EXT4=OFF")
    endif()
endif()

FetchContent_Declare(libusbhsfs
    GIT_REPOSITORY https://github.com/ITotalJustice/libusbhsfs.git
    GIT_TAG 625269b
)

FetchContent_MakeAvailable(libusbhsfs)

target_compile_definitions(sphaira PRIVATE ENABLE_LIBUSBHSFS)
target_link_libraries(sphaira PRIVATE libusbhsfs)

if (ENABLE_NTFS)
    target_compile_definitions(sphaira PRIVATE ENABLE_NTFS)
endif()

if (ENABLE_EXT4)
    target_compile_definitions(sphaira PRIVATE ENABLE_EXT4)
endif()

find_path(mbedtls_inc mbedtls REQUIRED)

# ztsd
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_COMPRESSION OFF)
set(ZSTD_MULTITHREAD_SUPPORT OFF)
set(ZSTD_BUILD_DECOMPRESSION ON)
set(ZSTD_BUILD_DICTBUILDER OFF)
set(ZSTD_LEGACY_SUPPORT OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_TESTS OFF)

# minini
set(MININI_LIB_NAME minIni)
set(MININI_USE_STDIO ON)
set(MININI_USE_NX OFF)
set(MININI_USE_FLOAT ON)

# nanovg
if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(NANOVG_DEBUG ON)
endif()
set(NANOVG_NO_JPEG OFF)
set(NANOVG_NO_PNG OFF)
set(NANOVG_NO_BMP OFF)
set(NANOVG_NO_PSD ON)
set(NANOVG_NO_TGA ON)
set(NANOVG_NO_GIF ON)
set(NANOVG_NO_HDR ON)
set(NANOVG_NO_PIC ON)
set(NANOVG_NO_PNM ON)

# yyjson
set(YYJSON_INSTALL OFF)
set(YYJSON_DISABLE_READER OFF)
set(YYJSON_DISABLE_WRITER OFF)
set(YYJSON_DISABLE_UTILS ON)
set(YYJSON_DISABLE_FAST_FP_CONV ON)
set(YYJSON_DISABLE_NON_STANDARD ON)
set(YYJSON_DISABLE_UTF8_VALIDATION ON)
set(YYJSON_DISABLE_UNALIGNED_MEMORY_ACCESS OFF)

FetchContent_MakeAvailable(
    libpulsar
    nanovg
    stb
    minIni
    yyjson
    zstd
    libnxtc
)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

find_package(ZLIB REQUIRED)
find_path(zlib_inc zlib.h)

add_library(libnxtc
    ${libnxtc_SOURCE_DIR}/source/nxtc.c
    ${libnxtc_SOURCE_DIR}/source/nxtc_log.c
    ${libnxtc_SOURCE_DIR}/source/nxtc_utils.c
)
target_include_directories(libnxtc PUBLIC ${libnxtc_SOURCE_DIR}/include ${zlib_inc})

find_library(minizip_lib minizip REQUIRED)
find_path(minizip_inc minizip REQUIRED)

find_package(CURL REQUIRED)
find_library(mbedcrypto_lib mbedcrypto REQUIRED)

set_target_properties(sphaira PROPERTIES
    C_STANDARD 23
    C_EXTENSIONS ON
    CXX_STANDARD 26
    CXX_EXTENSIONS ON
)

target_link_libraries(sphaira PRIVATE
    libpulsar
    minIni
    nanovg
    stb
    yyjson
    libnxtc
    libzstd_static

    ${minizip_lib}
    ZLIB::ZLIB
    CURL::libcurl
    ${mbedcrypto_lib}
)

target_include_directories(sphaira PRIVATE
    include
    ${minizip_inc}
    ${mbedtls_inc}
    include/yati/nx/nxdumptool
)

# copy the romfs
file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# create assets target
dkp_add_asset_target(sphaira_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs)

file(GLOB_RECURSE sphaira_romfs_source_files CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    ${CMAKE_SOURCE_DIR}/assets/romfs/*
)
add_custom_target(sphaira_romfs_sync
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/romfs
        ${CMAKE_CURRENT_BINARY_DIR}/romfs
    DEPENDS ${sphaira_romfs_source_files}
)
add_dependencies(sphaira_romfs sphaira_romfs_sync)
set_property(TARGET sphaira_romfs APPEND PROPERTY DKP_ASSET_FILES ${sphaira_romfs_source_files})

# wait until hbl is built first as we need the exefs to embed
add_dependencies(sphaira hbl_nso hbl_npdm)

# set the embed path for assets and hbl
target_compile_options(sphaira PRIVATE
    --embed-dir=${CMAKE_SOURCE_DIR}/assets/embed
    --embed-dir=${CMAKE_BINARY_DIR}/hbl
)

# create nacp
nx_generate_nacp(
    OUTPUT sphaira.nacp
    NAME "${APP_NAME}"
    AUTHOR "${APP_AUTHOR}"
    VERSION ${sphaira_VERSION}
)

# create nro
nx_create_nro(sphaira
    OUTPUT ${CMAKE_BINARY_DIR}/${APP_NRO}
    ICON ${CMAKE_SOURCE_DIR}/assets/icon.jpg
    NACP sphaira.nacp
    ROMFS sphaira_romfs
)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/switch/${APP_FOLDER})

add_custom_command(
    TARGET sphaira_nro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/${APP_NRO}
        ${CMAKE_BINARY_DIR}/switch/${APP_FOLDER}/${APP_NRO}
)

# read by build_release.sh and the release workflow
file(WRITE ${CMAKE_BINARY_DIR}/release.env
    "APP_VERSION=${APP_VERSION}\n"
    "APP_FOLDER=${APP_FOLDER}\n"
    "APP_NRO=${APP_NRO}\n"
    "APP_UPDATE_REPO=${APP_UPDATE_GITHUB_REPO}\n"
)

message(STATUS "generating nro in: ${CMAKE_BINARY_DIR}/${APP_NRO}")
message(STATUS "run nxlink -s ${CMAKE_BINARY_DIR}/${APP_NRO}")
