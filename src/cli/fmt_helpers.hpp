//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_CLI_FMT_HELPERS_HPP_INCLUDED
#define OCVSMD_CLI_FMT_HELPERS_HPP_INCLUDED

#include "ocvsmd/sdk/defines.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <uavcan/_register/Value_1_0.hpp>

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>

#include <string>
#include <type_traits>

template <>
struct fmt::formatter<uavcan::_register::Value_1_0> : formatter<std::string>
{
private:
    using TypeOf = uavcan::_register::Value_1_0::_traits_::TypeOf;

    static auto format_to(const TypeOf::empty&, format_context& ctx)
    {
        return fmt::format_to(ctx.out(), "empty");
    }

    static auto format_to(const TypeOf::string& str, format_context& ctx)
    {
        return fmt::format_to(ctx.out(), "'{}'", std::string{str.value.begin(), str.value.end()});
    }

    template <typename T>
    static auto format_to(const T& arr, format_context& ctx)
    {
        return fmt::format_to(ctx.out(), "{}", arr.value);
    }

public:
    static auto format(const uavcan::_register::Value_1_0& value, format_context& ctx)
    {
        return cetl::visit([&ctx](const auto& val) { return format_to(val, ctx); }, value.union_value);
    }
};

template <>
struct fmt::formatter<ocvsmd::sdk::Error> : formatter<std::string>
{
    static auto format(const ocvsmd::sdk::Error error, format_context& ctx)
    {
        using ocvsmd::sdk::Error;

        const char* error_name = "ErrorCode";
        switch (error.getCode())
        {
        case Error::Code::Other:
            error_name = "Other";
            break;
        case Error::Code::Busy:
            error_name = "Busy";
            break;
        case Error::Code::NoEntry:
            error_name = "NoEntry";
            break;
        case Error::Code::TimedOut:
            error_name = "TimedOut";
            break;
        case Error::Code::OutOfMemory:
            error_name = "OutOfMemory";
            break;
        case Error::Code::AlreadyExists:
            error_name = "AlreadyExists";
            break;
        case Error::Code::InvalidArgument:
            error_name = "InvalidArgument";
            break;
        case Error::Code::Canceled:
            error_name = "Canceled";
            break;
        case Error::Code::NotConnected:
            error_name = "NotConnected";
            break;
        case Error::Code::Disconnected:
            error_name = "Disconnected";
            break;
        case Error::Code::Shutdown:
            error_name = "Shutdown";
            break;
        case Error::Code::OperationInProgress:
            error_name = "OperationInProgress";
            break;
        default:
            CETL_DEBUG_ASSERT(false, "Unknown error code.");
            error_name = "ErrorCode";
            break;
        }

        if (const auto opt_errno = error.getOptErrno())
        {
            return format_to(ctx.out(), "{}(errno={})", error_name, opt_errno.value_or(0));
        }
        return format_to(ctx.out(), "{}()", error_name);
    }
};

#endif  // OCVSMD_CLI_FMT_HELPERS_HPP_INCLUDED
