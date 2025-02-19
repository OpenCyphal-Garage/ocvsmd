#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

add_executable(common_tests
        main.cpp
        io/test_socket_address.cpp
        ipc/test_client_router.cpp
        ipc/test_server_router.cpp
)
target_link_libraries(common_tests
        ocvsmd_common
        GTest::gmock
)

gtest_discover_tests(common_tests)
