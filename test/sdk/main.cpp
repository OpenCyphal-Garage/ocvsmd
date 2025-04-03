//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "gtest_printer.hpp"

#include <gtest/gtest.h>

#include <memory>

int main(int argc, char** const argv)
{
    ocvsmd::GtestPrinter::setupLogging(argc, argv, "sdk_tests");

    testing::InitGoogleTest(&argc, argv);

    // Adds a listener to the end. GoogleTest takes ownership.
    //
    auto  printer   = std::make_unique<ocvsmd::GtestPrinter>();
    auto& listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(printer.release());

    return RUN_ALL_TESTS();
}
