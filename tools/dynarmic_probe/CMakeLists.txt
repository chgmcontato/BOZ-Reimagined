# Headless Dynarmic probe. Standalone: the stable Makefile does not see this directory.
#   cmake -S tools/dynarmic_probe -B build/dynarmic-probe -G Ninja \
#         -DDYNARMIC_DIR=/path/to/dynarmic -DBoost_INCLUDE_DIR=/path/to/boost/include
#   cmake --build build/dynarmic-probe
#   build/dynarmic-probe/dynarmic_probe build/game/original/assets/boz.s3e.unpacked 5
cmake_minimum_required(VERSION 3.20)
project(dynarmic_probe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)

set(DYNARMIC_DIR "" CACHE PATH "Dynarmic source tree (e.g. azahar-emu/dynarmic)")
if(NOT DYNARMIC_DIR)
  message(FATAL_ERROR "set -DDYNARMIC_DIR=<dynarmic checkout>")
endif()
set(DYNARMIC_FRONTENDS A32 CACHE STRING "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
add_subdirectory(${DYNARMIC_DIR} dynarmic EXCLUDE_FROM_ALL)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
add_executable(dynarmic_probe probe.cpp ${SRC}/loader.c)
target_include_directories(dynarmic_probe PRIVATE ${SRC})
target_compile_definitions(dynarmic_probe PRIVATE _GNU_SOURCE)
# Guest pointers are identity-mapped in the low 4 GiB: keep the host image out of the way.
target_link_options(dynarmic_probe PRIVATE -no-pie)
target_link_libraries(dynarmic_probe PRIVATE dynarmic)
