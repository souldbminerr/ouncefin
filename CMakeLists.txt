cmake_minimum_required(VERSION 3.18)

# build options
set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# Include Platform common options
# example: cmake -B build -DPLATFORM_DESKTOP=ON
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

option(USE_SYSTEM_CURL "use system installed libcurl" ON)
option(USE_MUPDF "use mupdf for ebook" OFF)

# Linux
# Using CMAKE_INSTALL_PREFIX specifies the location where the application is installed.
cmake_dependent_option(CMAKE_INSTALL "Install to system path" OFF "UNIX;NOT APPLE" OFF)

# NintendoSwitch
cmake_dependent_option(BUILTIN_NSP "Built in NSP forwarder" OFF "PLATFORM_SWITCH" OFF)
cmake_dependent_option(USE_LIBUSBHSFS "Use libusbhsfs for ums access" OFF "PLATFORM_SWITCH" OFF)

# mpv related
# If your system does not support OpenGL(ES), you can use software rendering, but it will affect performance.
option(MPV_SW_RENDER "Using CPU to draw videos" OFF)

# Nintendo Switch 2 (libounce + drako3d, software video decode)
# drako3d v0.5.0 (Tegra T239/Ampere, deko3d-compatible Dk*/dk:: API) ships in
# $DEVKITPRO/libounce (headers + libdrako3d.a/libdrako3dd.a). UI rendering goes
# through drako3d (Borealis Switch video path); video frames go through
# MPV_SW_RENDER (FFmpeg SW decode -> RGBA upload to nanovg-dk), so no mpv GPU
# backend is needed.
option(PLATFORM_SWITCH2 "build for Switch 2 (libounce + drako3d, software decode)" OFF)
option(USE_DRAKO3D "Using drako3d instead of deko3d/OpenGL (Switch 2, requires drako3d in libounce)" OFF)
if (PLATFORM_SWITCH2)
    message(STATUS "PLATFORM_SWITCH2: libounce + drako3d + software video path")
    set(PLATFORM_SWITCH ON)
    set(MPV_SW_RENDER ON CACHE BOOL "Using CPU to draw videos" FORCE)
    set(BUILTIN_NSP OFF CACHE BOOL "" FORCE)
    set(USE_LIBUSBHSFS OFF CACHE BOOL "" FORCE)
    # deko3d targets Switch 1 Maxwell only. With USE_DRAKO3D, reuse the
    # borealis deko3d path (API-compatible) compiled against drako3d via
    # cmake/switch2/drako3d-compat; without it, use OunceVK OpenGL.
    if (USE_DRAKO3D)
        set(USE_DEKO3D ON CACHE BOOL "" FORCE)
    else ()
        set(USE_DEKO3D OFF CACHE BOOL "" FORCE)
    endif ()
    list(APPEND APP_PLATFORM_OPTION -D__SWITCH2__)
    # Official Switch2 toolchain provides switch2 pkg-config wrapper; borealis
    # toolchain.cmake otherwise pulls Switch 1 Switch.cmake which resets it to
    # the switch-only wrapper (missing switch2 mpv). Force switch2 wrapper.
    # Use msys2 (not PowerShell): source $DEVKITPRO/switch2vars.sh first.
    # Ninja generator is required (Unix Makefiles choke on C:/ paths).
    if (DEFINED ENV{DEVKITPRO})
        set(PKG_CONFIG_EXECUTABLE "$ENV{DEVKITPRO}/portlibs/switch2/bin/aarch64-none-elf-pkg-config" CACHE FILEPATH "" FORCE)
    endif ()
    if (USE_DRAKO3D)
        # drako3d API is deko3d-compatible; Borealis includes <deko3d.h/hpp>.
        # Compat shims live in-repo at cmake/switch2/drako3d-compat/.
        # Headers/libs come from $DEVKITPRO/libounce (installed by ounce-devkit).
        if (DEFINED ENV{DEVKITPRO})
            set(_LOUNCE_DIR "$ENV{DEVKITPRO}/libounce")
        else ()
            message(FATAL_ERROR "USE_DRAKO3D requires DEVKITPRO with libounce (source switch2vars.sh in msys2)")
        endif ()
        if (EXISTS "${_LOUNCE_DIR}/include/drako3d.hpp")
            set(DRako3D_INCLUDE_DIR "${_LOUNCE_DIR}/include" CACHE PATH "drako3d headers")
            # Borealis itself needs these (built in add_subdirectory below).
            include_directories("${DRako3D_INCLUDE_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/switch2/drako3d-compat")
            list(APPEND APP_PLATFORM_INCLUDE "${DRako3D_INCLUDE_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/switch2/drako3d-compat")
            list(APPEND APP_PLATFORM_OPTION -DBOREALIS_USE_DRAKO3D -DBOREALIS_USE_DEKO3D)
            message(STATUS "USE_DRAKO3D: ${_LOUNCE_DIR}, compat ${CMAKE_CURRENT_SOURCE_DIR}/cmake/switch2/drako3d-compat")
        else ()
            message(FATAL_ERROR "USE_DRAKO3D requested but ${_LOUNCE_DIR}/include/drako3d.hpp not found")
        endif ()
    endif ()
endif ()

if (MPV_SW_RENDER)
    list(APPEND APP_PLATFORM_OPTION -DMPV_SW_RENDER)
endif ()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
elseif (CMAKE_BUILD_TYPE STREQUAL Debug)
    list(APPEND APP_PLATFORM_OPTION -D_DEBUG -D_GLIBCXX_ASSERTIONS)
endif()

# Add git info
if (NOT DEFINED GIT_TAG_SHORT)
    execute_process(COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 5 OUTPUT_VARIABLE GIT_TAG_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif ()

if (NOT DEFINED VERSION_BUILD)
    execute_process(COMMAND git rev-list --count --all
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 5 OUTPUT_VARIABLE VERSION_BUILD
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif ()
message(STATUS "git commit ${GIT_TAG_SHORT} build ${VERSION_BUILD}")

# toolchain
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# project info
project(Switchfin)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "9")
set(VERSION_ALTER "5")
set(PROJECT_TITLEID "010ff000ffff0003")
set(PROJECT_AUTHOR "dragonflylee")
set(PACKAGE_NAME "fun.dragonfly.switchfin")
set(VITA_TITLEID "SWITCHFIN")
set(PSN_TITLE_ID "SFIN00000")
set(VITA_VERSION "00.95")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

if (CUSTOM_RESOURCES_DIR)
    set(BRLS_RESOURCES_DIR ${CUSTOM_RESOURCES_DIR})
elseif (CMAKE_INSTALL)
    set(BRLS_RESOURCES_DIR ${CMAKE_INSTALL_PREFIX}/share/switchfin)
endif ()

# setting src and include
file(GLOB_RECURSE MAIN_SRC "${PROJECT_SOURCE_DIR}/app/src/*.cpp")
if (PLATFORM_SWITCH)
    # System service initialization functions required for libnx
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
elseif (PLATFORM_PS4)
    # Show backtrace when app crash
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/ps4/crashlog.c)
elseif (PLATFORM_PSV)
    configure_file("${CMAKE_SOURCE_DIR}/app/platform/vita/template.xml.in" "${CMAKE_BINARY_DIR}/template.xml")
elseif (WIN32)
    configure_file("${CMAKE_SOURCE_DIR}/app/platform/win32/app.rc.in" "${CMAKE_BINARY_DIR}/app.rc")
    list(APPEND MAIN_SRC ${CMAKE_BINARY_DIR}/app.rc)
elseif (APPLE)
    list(APPEND MAIN_SRC ${CMAKE_SOURCE_DIR}/app/platform/osx/app.icns)
endif ()

if (USE_LIBROMFS)
    message(STATUS "USE LIBROMFS")
    add_definitions(-DUSE_LIBROMFS)
    set(LIBROMFS_PROJECT_NAME ${PROJECT_NAME})
    set(LIBROMFS_RESOURCE_LOCATION "${PROJECT_RESOURCES}")
endif ()

# build borealis and other third party libraries
add_subdirectory(library)

if (PLATFORM_SWITCH2)
    # With USE_DRAKO3D, borealis builds its deko3d path (nanovg-dk), which
    # links `deko3d` (Maxwell). Swap it for drako3d (Ampere, API-compatible).
    # Without USE_DRAKO3D (OunceVK fallback), drop Tegra X1 Nouveau libs.
    get_target_property(_brls_libs borealis LINK_LIBRARIES)
    if (USE_DRAKO3D)
        # deko3d (Maxwell) -> drako3d (Ampere) in both private and
        # propagated interface libs. Match whole list items only.
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_drako_lib drako3dd)
        else ()
            set(_drako_lib drako3d)
        endif ()
        foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_libs borealis ${_prop})
            if (_libs)
                set(_fixed "")
                foreach(_l IN LISTS _libs)
                    if (_l STREQUAL "deko3d")
                        list(APPEND _fixed "${_drako_lib}")
                    else ()
                        list(APPEND _fixed "${_l}")
                    endif ()
                endforeach ()
                set_target_properties(borealis PROPERTIES ${_prop} "${_fixed}")
            endif ()
        endforeach ()
        list(APPEND APP_PLATFORM_LIB drako3d nx m)
    else ()
        if (_brls_libs)
            list(REMOVE_ITEM _brls_libs drm_nouveau)
            set_target_properties(borealis PROPERTIES LINK_LIBRARIES "${_brls_libs}")
        endif ()
        # Ensure OunceVK EGL is linked for the app as well (harmless if already present)
        list(APPEND APP_PLATFORM_LIB EGL GLESv2)
    endif ()
endif ()

if (USE_SYSTEM_CURL)
    find_package(CURL REQUIRED)
else ()
    include(FetchContent)
    FetchContent_Declare(curl
        URL https://curl.se/download/curl-8.16.0.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    if (WIN32)
        set(CURL_USE_SCHANNEL ON)
        set(USE_WIN32_IDN ON)
        set(ZLIB_USE_STATIC_LIBS ON)
    elseif (PLATFORM_ANDROID)
        set(CURL_USE_MBEDTLS ON)
    else ()
        find_package(OpenSSL REQUIRED)
        set(CURL_USE_OPENSSL ON)
    endif()
    set(HTTP_ONLY ON)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(ENABLE_CURL_MANUAL OFF)
    set(USE_NGHTTP2 OFF)
    set(USE_LIBIDN2 OFF)
    set(CURL_BROTLI OFF)
    set(CURL_ZSTD OFF)
    set(CURL_USE_LIBSSH2 OFF)
    set(CURL_USE_LIBPSL OFF)
    set(CURL_DISABLE_PROGRESS_METER ON)
    FetchContent_MakeAvailable(curl)
endif ()
list(APPEND APP_PLATFORM_LIB CURL::libcurl)

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

if (USE_BOOST_FILESYSTEM)
    find_package(Boost 1.44 REQUIRED COMPONENTS filesystem)
    if(Boost_FOUND)
        list(APPEND APP_PLATFORM_LIB Boost::filesystem)
        list(APPEND APP_PLATFORM_OPTION -DUSE_BOOST_FILESYSTEM -DBOOST_FILESYSTEM_VERSION=4)
    endif()
endif ()

if (USE_MUPDF)
    find_package(MuPDF REQUIRED)
    list(APPEND APP_PLATFORM_OPTION -DUSE_MUPDF)
    list(APPEND APP_PLATFORM_LIB ${MuPDF_LIBRARY})
endif ()

if (PLATFORM_DESKTOP)
    find_package(MPV REQUIRED)
    list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIR})
    list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARY})
    if (NOT WIN32)
        find_library(AVFORMAT_LIB NAMES avformat REQUIRED)
        list(APPEND APP_PLATFORM_LIB ${AVFORMAT_LIB})
    endif ()

    find_package(WebP)
    if (WebP_FOUND)
        list(APPEND APP_PLATFORM_OPTION -DUSE_WEBP)
        list(APPEND APP_PLATFORM_INCLUDE ${WebP_INCLUDE_DIRS})
        list(APPEND APP_PLATFORM_LIB ${WebP_LIBRARY})
    endif ()

    if (MINGW)
        set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static")
    endif ()
else ()
    # Used for Cross Compile such as NintendoSwitch
    # If dependent files cannot be found, environment variables can be set before executing the cmake command:
    # PKG_CONFIG_LIBDIR=/some/path/arm64/lib/pkgconfig
    find_package(PkgConfig REQUIRED)

    pkg_search_module(MPV REQUIRED mpv)
    list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIRS})
    list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARIES})
    link_directories(${MPV_LIBRARY_DIRS})

    pkg_search_module(LIBWEBP QUIET libwebp)
    if (LIBWEBP_FOUND)
        message(STATUS "Found libwebp: ${LIBWEBP_INCLUDE_DIRS} ${LIBWEBP_LIBRARIES}")
        list(APPEND APP_PLATFORM_INCLUDE ${LIBWEBP_INCLUDE_DIRS})
        list(APPEND APP_PLATFORM_LIB ${LIBWEBP_LIBRARIES})
        list(APPEND APP_PLATFORM_OPTION -DUSE_WEBP)
        link_directories(${LIBWEBP_LIBRARY_DIRS})
    endif ()
endif()

if (PLATFORM_SWITCH2)
    # switch2 ffmpeg enables mbedtls (TLS); its static libs must come after
    # curl/ffmpeg on the link line (switch portlibs has them, switch2 does not).
    list(APPEND APP_PLATFORM_LIB mbedtls mbedx509 mbedcrypto)
endif ()

if (BUILTIN_NSP)
    find_library(NSP_LIB NAMES nsp REQUIRED)
    list(APPEND APP_PLATFORM_LIB nsp)
    list(APPEND APP_PLATFORM_OPTION -DBUILTIN_NSP)
    add_subdirectory(scripts/forwarder)
endif ()

if (USE_LIBUSBHSFS)
    find_library(USBHSFS_LIB NAMES usbhsfs REQUIRED)
    list(APPEND APP_PLATFORM_LIB lwext4 ntfs-3g usbhsfs)
    list(APPEND APP_PLATFORM_OPTION -DUSE_LIBUSBHSFS)
endif ()

# building target
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE app/include ${APP_PLATFORM_INCLUDE})
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections -Wunused-variable ${APP_PLATFORM_OPTION})
target_link_libraries(${PROJECT_NAME} PRIVATE borealis lunasvg ${APP_PLATFORM_LIB})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION=${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}
    APP_BUILD=${VERSION_BUILD}
    BUILD_TAG_SHORT=${GIT_TAG_SHORT}
    BUILD_PACKAGE_NAME=${PROJECT_NAME}
)

# building release file
if (PLATFORM_DESKTOP)
    if (APPLE)
        if (NOT CMAKE_OSX_ARCHITECTURES)
            set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
            set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
            set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET[arch=arm64] "11.0" CACHE STRING "" FORCE)
        endif ()
        message(STATUS "CMAKE_OSX_ARCHITECTURES: ${CMAKE_OSX_ARCHITECTURES}")

        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
            MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
            MACOSX_BUNDLE_LONG_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}"
            MACOSX_BUNDLE_ICON_FILE "app.icns"
            MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 ${PROJECT_AUTHOR}"
            RESOURCE "${CMAKE_SOURCE_DIR}/app/platform/osx/app.icns"
        )

        if (CMAKE_PREFIX_PATH)
            set(BUNDLE_ARG -s ${CMAKE_PREFIX_PATH}/lib)
        endif ()

        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} Resources/resources
            COMMAND otool -L MacOS/${PROJECT_NAME}
            COMMAND dylibbundler -cd -ns -b ${BUNDLE_ARG} -x MacOS/${PROJECT_NAME} -d Frameworks/ -p "@executable_path/../Frameworks/"
            COMMAND codesign --sign - --force Frameworks/*
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/${PROJECT_NAME}.app/Contents"
        )
    elseif (CMAKE_INSTALL)
        set(icon_dirs 32x32 48x48 64x64 128x128 256x256)
        linux_install(
            "${PROJECT_RESOURCES}" "${BRLS_RESOURCES_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/switchfin.svg"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/icons" "${icon_dirs}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${PACKAGE_NAME}.appdata.xml"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${PACKAGE_NAME}.desktop"
        )
    elseif (NOT USE_LIBROMFS)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} ${PROJECT_BINARY_DIR}/resources
        )
    endif()

elseif (PLATFORM_SWITCH)
    if (BUILTIN_NSP)
        add_dependencies(${PROJECT_NAME} forwarder.nsp)
    endif ()
    if (USE_DEKO3D AND NOT PLATFORM_SWITCH2)
        gen_dksh("${PROJECT_RESOURCES}/shaders")
    endif ()
    if (PLATFORM_SWITCH2)
        message(STATUS "Switch 2 packaging: NRO via libounce (verify with ounce-devkit; target name may change to NRO2/NSP2)")
        if (USE_DRAKO3D)
            # Ampere DKSH for nanovg-dk (drako3d) via uam2. Generated at build
            # time into the romfs staging dir (after the resources copy).
            find_program(NX_UAM2_EXE NAMES uam2 HINTS "${DEVKITPRO}/tools/bin")
            if (NOT NX_UAM2_EXE)
                message(FATAL_ERROR "USE_DRAKO3D needs uam2 (devkitPro tools/bin)")
            endif ()
            set(_DKSH_SRC "${BOREALIS_LIBRARY}/lib/extern/nanovg/deko3d/shaders")
            add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
                COMMAND ${NX_NACPTOOL_EXE} --create "${PROJECT_NAME}" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
                COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/resources/font/keymap*.ttf ${CMAKE_BINARY_DIR}/resources/font/*_fallback.ttf ${CMAKE_BINARY_DIR}/resources/font/switch_font.ttf
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/resources/shaders
                COMMAND ${NX_UAM2_EXE} -s vert -o ${CMAKE_BINARY_DIR}/resources/shaders/fill_vsh.dksh ${_DKSH_SRC}/fill_vsh.glsl
                COMMAND ${NX_UAM2_EXE} -s frag -o ${CMAKE_BINARY_DIR}/resources/shaders/fill_fsh.dksh ${_DKSH_SRC}/fill_fsh.glsl
                COMMAND ${NX_UAM2_EXE} -s frag -o ${CMAKE_BINARY_DIR}/resources/shaders/fill_aa_fsh.dksh ${_DKSH_SRC}/fill_aa_fsh.glsl
                COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
            )
        else ()
            add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
                COMMAND ${NX_NACPTOOL_EXE} --create "${PROJECT_NAME}" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
                COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/resources/font/keymap*.ttf ${CMAKE_BINARY_DIR}/resources/font/*_fallback.ttf ${CMAKE_BINARY_DIR}/resources/font/switch_font.ttf
                COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
            )
        endif ()
    else ()
        add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
            COMMAND ${NX_NACPTOOL_EXE} --create "${PROJECT_NAME}" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
            COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/resources/font/keymap*.ttf ${CMAKE_BINARY_DIR}/resources/font/*_fallback.ttf ${CMAKE_BINARY_DIR}/resources/font/switch_font.ttf
            COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
        )
    endif ()
elseif (PLATFORM_PSV)
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12") # max heap size mode
    set(PSV_ASSETS_FILES
        FILE ${CMAKE_SOURCE_DIR}/app/platform/vita/sce_sys/icon0.png sce_sys/icon0.png
        FILE ${CMAKE_SOURCE_DIR}/app/platform/vita/sce_sys/pic0.png sce_sys/pic0.png
        FILE ${CMAKE_SOURCE_DIR}/app/platform/vita/sce_sys/bg.png sce_sys/livearea/contents/bg.png
        FILE ${CMAKE_SOURCE_DIR}/app/platform/vita/sce_sys/startup.png sce_sys/livearea/contents/startup.png
        FILE ${CMAKE_BINARY_DIR}/template.xml sce_sys/livearea/contents/template.xml
    )
    if (NOT USE_LIBROMFS)
        list(APPEND PSV_ASSETS_FILES ${CMAKE_SOURCE_DIR}/resources resources)
    endif ()
    if (USE_VITA_SHARK)
        list(APPEND PSV_ASSETS_FILES "${CMAKE_BINARY_DIR}/vendor/SceShaccCg" module)
    endif ()
    vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE NOASLR)
    vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
        VERSION ${VITA_VERSION}
        NAME ${PROJECT_NAME}
        FILE ${PSV_ASSETS_FILES}
    )
elseif (PLATFORM_PS4)
    add_self(${PROJECT_NAME})
    add_pkg(${PROJECT_NAME}
            ${CMAKE_BINARY_DIR}/ps4
            ${PSN_TITLE_ID}
            "switchfin"
            ${VITA_VERSION}
            8388608
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${BOREALIS_DIR}/ps4/sce_module ${CMAKE_BINARY_DIR}/ps4/sce_module
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${CMAKE_SOURCE_DIR}/app/platform/ps4/sce_sys ${CMAKE_BINARY_DIR}/ps4/sce_sys
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${BOREALIS_DIR}/ps4/sce_sys/about ${CMAKE_BINARY_DIR}/ps4/sce_sys/about
        COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/ps4/resources
    )
endif ()
