#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
)
block()
    set("CMAKE_CXX_CLANG_TIDY" "")
    FetchContent_MakeAvailable(googletest)
endblock()

include(GoogleTest)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(common)
add_subdirectory(daemon)
add_subdirectory(cli)
add_subdirectory(sdk)
