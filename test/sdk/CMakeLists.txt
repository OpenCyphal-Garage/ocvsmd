#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

add_executable(sdk_tests
        main.cpp
        svc/relay/test_raw_publisher_client.cpp
)
target_link_libraries(sdk_tests
        ocvsmd_common
        ocvsmd_sdk
        GTest::gmock
)
target_include_directories(sdk_tests
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

gtest_discover_tests(sdk_tests)
