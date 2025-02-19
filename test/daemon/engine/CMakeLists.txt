#
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT
#

cmake_minimum_required(VERSION 3.25)

add_executable(engine_tests
        main.cpp
        svc/node/test_exec_cmd_service.cpp
)
target_link_libraries(engine_tests
        ocvsmd_engine
        GTest::gmock
)

gtest_discover_tests(engine_tests)
