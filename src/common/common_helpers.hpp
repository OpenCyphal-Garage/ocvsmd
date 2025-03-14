//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_HELPERS_HPP_INCLUDED
#define OCVSMD_COMMON_HELPERS_HPP_INCLUDED

#include "ocvsmd/sdk/defines.hpp"

#include <cetl/cetl.hpp>

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

/// Converts an `errno` positive integer to an error code.
///
/// `0` is converted to `nullopt`.
/// Internally in use to convert raw integers (from DSDL types) to optional error codes.
///
inline sdk::ErrorCode errnoToErrorCode(const int errno_value) noexcept
{
    CETL_DEBUG_ASSERT(errno_value > 0, "Positive Posix error number is expected.");

    return static_cast<sdk::ErrorCode>(errno_value);
}

/// Converts a raw integer to an optional error code.
///
/// `0` is converted to `nullopt`.
/// Internally in use to convert raw integers (from DSDL types) to optional error codes.
///
inline sdk::OptErrorCode rawIntToOptErrorCode(const std::underlying_type_t<sdk::ErrorCode> raw_error_code) noexcept
{
    return raw_error_code != 0 ? sdk::OptErrorCode{static_cast<sdk::ErrorCode>(raw_error_code)}  //
                               : sdk::OptErrorCode{};
}

/// Converts an error code to its underlying raw integers.
///
/// Internally in use to convert error codes to raw integers (for DSDL types).
///
inline std::underlying_type_t<sdk::ErrorCode> errorCodeToRawInt(const sdk::ErrorCode error_code) noexcept
{
    return static_cast<std::underlying_type_t<sdk::ErrorCode>>(error_code);
}

/// Converts an optional error code to its underlying raw integers.
///
/// `nullopt` is converted to `0`.
/// Internally in use to convert optional error codes to raw integers (for DSDL types).
///
inline std::underlying_type_t<sdk::ErrorCode> optErrorCodeToRawInt(const sdk::OptErrorCode error_code) noexcept
{
    return error_code.has_value() ? errorCodeToRawInt(*error_code) : 0;
}

}  // namespace common
}  // namespace ocvsmd

// MARK: - Formatting

// NOLINTBEGIN
template <>
struct fmt::formatter<ocvsmd::sdk::ErrorCode> : formatter<std::string>
{
    static auto format(const ocvsmd::sdk::ErrorCode error_code, format_context& ctx)
    {
        using ocvsmd::sdk::ErrorCode;

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
        return format_to(ctx.out(), "{}({})", error_name, ocvsmd::common::errorCodeToRawInt(error_code));
    }
};
template <>
struct fmt::formatter<ocvsmd::sdk::OptErrorCode> : formatter<std::string>
{
    static auto format(const ocvsmd::sdk::OptErrorCode error_code, format_context& ctx)
    {
        return error_code.has_value() ? formatter<ocvsmd::sdk::ErrorCode>::format(*error_code, ctx)
                                      : format_to(ctx.out(), "null");
    }
};
// NOLINTEND

#endif  // OCVSMD_COMMON_HELPERS_HPP_INCLUDED
