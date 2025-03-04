#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

project(ocvsmd
        LANGUAGES C CXX
        HOMEPAGE_URL https://github.com/OpenCyphal-Garage/opencyphal-vehicle-system-management-daemon
)

enable_testing()

set(NO_STATIC_ANALYSIS OFF CACHE BOOL "disable static analysis")

set(CMAKE_CXX_STANDARD 14 CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set the output binary directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(src_dir "${CMAKE_SOURCE_DIR}/src")
set(test_dir "${CMAKE_SOURCE_DIR}/test")
set(include_dir "${CMAKE_SOURCE_DIR}/include")
set(submodules_dir "${CMAKE_SOURCE_DIR}/submodules")

set(CXX_FLAG_SET "")
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Disable PSABI warnings in GCC (on RPi).
    list(APPEND CXX_FLAG_SET "-Wno-psabi")
endif()
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${CXX_FLAG_SET}>")

# clang-format
find_program(clang_format NAMES clang-format)
if (NOT clang_format)
    message(STATUS "Could not locate clang-format")
else ()
    file(GLOB_RECURSE format_files
            ${include_dir}/ocvsmd/*.hpp
            ${src_dir}/*.[ch]
            ${src_dir}/*.[ch]pp
            ${test_dir}/*.[ch]pp
    )
    message(STATUS "Using clang-format: ${clang_format}")
    add_custom_target(format COMMAND ${clang_format} -i -fallback-style=none -style=file --verbose ${format_files})
endif ()

# Use -DNO_STATIC_ANALYSIS=1 to suppress static analysis.
# If not suppressed, the tools used here shall be available, otherwise the build will fail.
if (NOT NO_STATIC_ANALYSIS)
    # clang-tidy (separate config files per directory)
    find_program(clang_tidy NAMES clang-tidy)
    if (NOT clang_tidy)
        message(FATAL_ERROR "Could not locate clang-tidy")
    endif ()
    message(STATUS "Using clang-tidy: ${clang_tidy}")
    set(CMAKE_CXX_CLANG_TIDY ${clang_tidy})
endif ()

# Pull in Nunavut's cmake integration
find_package("Nunavut" 3.0 REQUIRED)

# libcyphal requires PMR support for Nunavut generated code.
if (${CMAKE_CXX_STANDARD} STREQUAL "14")
    set(CYPHAL_LANGUAGE_STANDARD "cetl++14-17")
else ()
    set(CYPHAL_LANGUAGE_STANDARD "c++${CMAKE_CXX_STANDARD}-pmr")
endif ()

# Forward the revision information to the compiler so that we could expose it at runtime. This is entirely optional.
execute_process(
        COMMAND git rev-parse --short=16 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE vcs_revision_id
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "vcs_revision_id: ${vcs_revision_id}")
add_definitions(
        -DVERSION_MAJOR=0
        -DVERSION_MINOR=1
        -DVCS_REVISION_ID=0x${vcs_revision_id}ULL
        -DNODE_NAME="org.opencyphal.ocvsmd"
)
if (DEFINED PLATFORM_OS_TYPE)
    if (${PLATFORM_OS_TYPE} STREQUAL "bsd")
        add_definitions(-DPLATFORM_OS_TYPE_BSD)
    elseif (${PLATFORM_OS_TYPE} STREQUAL "linux")
        add_definitions(-DPLATFORM_OS_TYPE_LINUX)
    endif ()
endif ()

add_subdirectory(src)
add_subdirectory(test)
