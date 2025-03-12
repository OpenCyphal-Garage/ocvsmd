//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_CLI_FMT_HELPERS_HPP_INCLUDED
#define OCVSMD_CLI_FMT_HELPERS_HPP_INCLUDED

#include "ocvsmd/sdk/defines.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <uavcan/_register/Value_1_0.hpp>

#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h>

#include <string>

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
struct fmt::formatter<ocvsmd::sdk::ErrorCode> : formatter<std::string>
{
    auto format(const ocvsmd::sdk::ErrorCode error_code, format_context& ctx) const
    {
        using ocvsmd::sdk::ErrorCode;

        if (error_code == ErrorCode::Success)
        {
            return format_to(ctx.out(), "Success");
        }

        const char* error_name = "ErrorCode";
        switch (error_code)
        {
        case ErrorCode::Busy:
            error_name = "Busy";
            break;
        case ErrorCode::NoEntry:
            error_name = "NoEntry";
            break;
        case ErrorCode::TimedOut:
            error_name = "TimedOut";
            break;
        case ErrorCode::OutOfMemory:
            error_name = "OutOfMemory";
            break;
        case ErrorCode::AlreadyExists:
            error_name = "AlreadyExists";
            break;
        case ErrorCode::InvalidArgument:
            error_name = "InvalidArgument";
            break;
        case ErrorCode::Canceled:
            error_name = "Canceled";
            break;
        case ErrorCode::NotConnected:
            error_name = "NotConnected";
            break;
        case ErrorCode::Disconnected:
            error_name = "Disconnected";
            break;
        case ErrorCode::Shutdown:
            error_name = "NotConnected";
            break;
        case ErrorCode::OperationInProgress:
            error_name = "OperationInProgress";
            break;
        default:
            error_name = "ErrorCode";
            break;
        }
        return format_to(ctx.out(), "{}({})", error_name, static_cast<std::int32_t>(error_code));
    }
};

#endif  // OCVSMD_CLI_FMT_HELPERS_HPP_INCLUDED
