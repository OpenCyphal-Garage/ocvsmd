#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

add_executable(ocvsmd-cli
        main.cpp
)
target_link_libraries(ocvsmd-cli
        PUBLIC ocvsmd_sdk
)
target_include_directories(ocvsmd-cli SYSTEM
        PUBLIC ${submodules_dir}/spdlog/include
)
add_dependencies(ocvsmd-cli
        ocvsmd
)
