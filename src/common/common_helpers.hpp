//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_HELPERS_HPP_INCLUDED
#define OCVSMD_COMMON_HELPERS_HPP_INCLUDED

#include "ocvsmd/sdk/defines.hpp"

#include <spdlog/spdlog.h>

#include <exception>
#include <type_traits>
#include <utility>

namespace ocvsmd
{
namespace common
{

/// @brief Wraps the given action into a try/catch block, and performs it without throwing the given exception type.
///
/// @return `true` if the action was performed successfully, `false` if an exception was thrown.
///         Always `true` if exceptions are disabled.
///
template <typename Exception = std::exception, typename Action>
bool performWithoutThrowing(Action&& action) noexcept
{
#if defined(__cpp_exceptions)
    try
    {
#endif
        std::forward<Action>(action)();
        return true;

#if defined(__cpp_exceptions)
    } catch (const Exception& ex)
    {
        spdlog::critical("Unexpected C++ exception is caught: {}", ex.what());
        return false;
    }
#endif
}

}  // namespace common
}  // namespace ocvsmd

// MARK: - Formatting

// NOLINTBEGIN
template <>
struct fmt::formatter<ocvsmd::sdk::ErrorCode> : formatter<std::string>
{
    auto format(const ocvsmd::sdk::ErrorCode error_code, format_context& ctx) const
    {
        using ocvsmd::sdk::ErrorCode;

        if (error_code == ocvsmd::sdk::ErrorCode::Success)
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
            error_name = "Shutdown";
            break;
        case ErrorCode::OperationInProgress:
            error_name = "OperationInProgress";
            break;
        default:
            error_name = "ErrorCode";
            break;
        }
        return format_to(ctx.out(), "{}({})", error_name, static_cast<std::underlying_type_t<ErrorCode>>(error_code));
    }
};
// NOLINTEND

#endif  // OCVSMD_COMMON_HELPERS_HPP_INCLUDED
