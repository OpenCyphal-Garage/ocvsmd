//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_GTEST_PRINTER_HPP_INCLUDED
#define OCVSMD_GTEST_PRINTER_HPP_INCLUDED

#include <spdlog/cfg/argv.h>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace ocvsmd
{

class GtestPrinter final : public testing::EmptyTestEventListener
{
public:
    /// Sets up the logging system.
    ///
    /// File sink is used for all loggers (with Trace default level).
    ///
    static void setupLogging(const int argc, char** const argv, const std::string& log_prefix)
    {
        try
        {
            // Drop all existing loggers, including the default one, so that we can reconfigure them.
            spdlog::drop_all();

            const std::string log_file_nm = log_prefix + ".log";
            const auto        file_sink   = std::make_shared<spdlog::sinks::basic_file_sink_st>(log_file_nm);

            const auto default_logger = std::make_shared<spdlog::logger>("", file_sink);
            default_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%P] [%n] [%l] %v");
            register_logger(default_logger);
            set_default_logger(default_logger);

            // Accept `SPDLOG_LEVEL` argument (like `SPDLOG_LEVEL=debug`).
            //
            spdlog::set_level(spdlog::level::trace);
            spdlog::cfg::load_argv_levels(argc, argv);

        } catch (const std::exception& ex)
        {
            std::cerr << "Failed to setup logging: " << ex.what() << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

private:
    // Fired before the test suite starts.
    void OnTestSuiteStart(const testing::TestSuite& test_suite) override
    {
        spdlog::info("====================> TEST_SUITE {}", test_suite.name());
    }

    // Called before a test starts.
    void OnTestStart(const testing::TestInfo& test_info) override
    {
        spdlog::info("--------------------------> TEST {}.{} 🔵…", test_info.test_suite_name(), test_info.name());
    }

    // Called after a failed assertion or a SUCCESS().
    void OnTestPartResult(const testing::TestPartResult& test_part_result) override
    {
        if (const auto* const file_name = test_part_result.file_name())
        {
            if (test_part_result.failed())
            {
                spdlog::error("TEST Failure in {}:{} ❌\n{}",
                              file_name,
                              test_part_result.line_number(),
                              test_part_result.summary());
            }
            else
            {
                spdlog::debug("TEST Success in {}:{}\n{}",
                              file_name,
                              test_part_result.line_number(),
                              test_part_result.summary());
            }
        }
        else
        {
            if (test_part_result.failed())
            {
                spdlog::error("TEST Failure ❌\n{}", test_part_result.summary());
            }
            else
            {
                spdlog::debug("TEST Success \n{}", test_part_result.summary());
            }
        }
    }

    // Called after a test ends.
    void OnTestEnd(const testing::TestInfo& test_info) override
    {
        spdlog::info("<-------------------------- TEST {}.{} 🏁.", test_info.test_suite_name(), test_info.name());
    }

    // Fired after the test suite ends.
    void OnTestSuiteEnd(const testing::TestSuite& test_suite) override
    {
        spdlog::info("<==================== TEST_SUITE {}", test_suite.name());
        spdlog::info("");
    }

    void OnTestProgramEnd(const testing::UnitTest&) override
    {
        spdlog::info("🏁.\n");
    }

};  // GtestPrinter

}  // namespace ocvsmd

#endif  // OCVSMD_GTEST_PRINTER_HPP_INCLUDED
