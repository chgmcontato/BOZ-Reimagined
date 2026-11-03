# BOZ Reimagined Mobile: the PC runtime (src/) built as the Android app's native library.
#
# The same file builds two things:
#   Android (Gradle/NDK, ANDROID is set): libmain.so for SDL's SDLActivity, plus libSDL2.so
#   anything else: boz-runtime, an executable of the same flavour (guest base + OpenGL ES) used to
#                  test this build on a desktop or an ARM64 Linux, without a phone
#
# Dependencies, as directories (mobile/android/app/build.gradle.kts downloads and checks them):
#   BOZ_SDL2_SOURCE_DIR   SDL 2 source tree (Android: built here; elsewhere optional, see below)
#   BOZ_BOOST_INCLUDE_DIR directory that holds boost/ (header-only use by Dynarmic)
#
# The PC build (Makefile) does not use this file.
cmake_minimum_required(VERSION 3.22)
project(boz_mobile C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(BOZ_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

option(BOZ_GUEST_BASE "Guest memory in a 4 GiB reservation instead of the identity map" ON)
option(BOZ_GLES "OpenGL ES 3 context instead of desktop OpenGL" ON)
set(BOZ_SDL2_SOURCE_DIR "" CACHE PATH "SDL 2 source tree")
set(BOZ_BOOST_INCLUDE_DIR "" CACHE PATH "Directory containing boost/")

if(NOT BOZ_BOOST_INCLUDE_DIR OR NOT EXISTS "${BOZ_BOOST_INCLUDE_DIR}/boost/icl/interval_set.hpp")
    message(FATAL_ERROR "BOZ_BOOST_INCLUDE_DIR must point at a directory with boost/ "
                        "(got '${BOZ_BOOST_INCLUDE_DIR}'). Gradle sets it; see mobile/README.md.")
endif()
if(NOT EXISTS "${BOZ_ROOT}/third_party/dynarmic/CMakeLists.txt")
    message(FATAL_ERROR "third_party/dynarmic is empty: git submodule update --init --recursive")
endif()

# ---- SDL 2 ----
if(ANDROID)
    if(NOT BOZ_SDL2_SOURCE_DIR)
        message(FATAL_ERROR "BOZ_SDL2_SOURCE_DIR is required on Android")
    endif()
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${BOZ_SDL2_SOURCE_DIR}" sdl2 EXCLUDE_FROM_ALL)
    set(BOZ_SDL_TARGET SDL2::SDL2)
elseif(BOZ_SDL2_SOURCE_DIR)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${BOZ_SDL2_SOURCE_DIR}" sdl2 EXCLUDE_FROM_ALL)
    set(BOZ_SDL_TARGET SDL2::SDL2-static)
else()
    find_package(SDL2 REQUIRED)
    set(BOZ_SDL_TARGET SDL2::SDL2)
endif()

# ---- Dynarmic (A32 frontend; the arm64 backend on a phone) ----
set(DYNARMIC_FRONTENDS "A32" CACHE STRING "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(Boost_INCLUDE_DIR "${BOZ_BOOST_INCLUDE_DIR}" CACHE PATH "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory("${BOZ_ROOT}/third_party/dynarmic" dynarmic EXCLUDE_FROM_ALL)

# ---- runtime ----
# Same list as the Makefile's SRC (POSIX flavour), plus the OpenGL ES stand-ins and, on
# Android, the platform glue.
set(BOZ_SOURCES
    src/aim_probe.c src/assets.c src/audio.c src/bindings.c src/config.c src/file.c src/gheap.c
    src/gl.c src/gl_state.c src/gl_thunks.c src/imports.c src/input.c src/install.c
    src/launcher.c src/loader.c src/main.c src/mdns_wire.c src/memory.c src/menus.c
    src/overlay.c src/pvrtc.c src/reimagined.c src/runtime.c src/screens.c src/socket.c
    src/timer.c src/ui.c src/updater.c src/zeroconf.c src/zeroconf_platform_posix.c
    src/coop_stats.c src/gles_compat.c src/emu_dynarmic.cpp
    third_party/cJSON.c third_party/LzmaDec.c)
if(ANDROID)
    list(APPEND BOZ_SOURCES src/platform_android.c)
endif()
list(TRANSFORM BOZ_SOURCES PREPEND "${BOZ_ROOT}/")

if(ANDROID)
    add_library(main SHARED ${BOZ_SOURCES})
    set(BOZ_TARGET main)
    # src/main.c's main() becomes boz_main; platform_android.c's SDL_main runs it on a thread
    # with a desktop-sized stack.
    set_source_files_properties("${BOZ_ROOT}/src/main.c" PROPERTIES
        COMPILE_DEFINITIONS "SDL_MAIN_HANDLED;main=boz_main")
else()
    add_executable(boz-runtime ${BOZ_SOURCES})
    set(BOZ_TARGET boz-runtime)
endif()

target_include_directories(${BOZ_TARGET} PRIVATE "${BOZ_ROOT}/src" "${BOZ_ROOT}/third_party/dynarmic/src")
target_compile_definitions(${BOZ_TARGET} PRIVATE _GNU_SOURCE)
if(BOZ_GUEST_BASE)
    target_compile_definitions(${BOZ_TARGET} PRIVATE CODBOZ_GUEST_BASE)
endif()
if(BOZ_GLES)
    target_compile_definitions(${BOZ_TARGET} PRIVATE CODBOZ_GLES)
endif()
set_source_files_properties("${BOZ_ROOT}/third_party/LzmaDec.c" PROPERTIES COMPILE_DEFINITIONS _7ZIP_ST)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${BOZ_TARGET} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wno-unused-parameter>)
endif()

target_link_libraries(${BOZ_TARGET} PRIVATE dynarmic ${BOZ_SDL_TARGET} m)
if(ANDROID)
    target_link_libraries(${BOZ_TARGET} PRIVATE log android)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(${BOZ_TARGET} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
