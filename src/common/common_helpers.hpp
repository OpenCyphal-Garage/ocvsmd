//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_HELPERS_HPP_INCLUDED
#define OCVSMD_COMMON_HELPERS_HPP_INCLUDED

#include "ipc/channel.hpp"
#include "ocvsmd/sdk/defines.hpp"

#include <ocvsmd/common/Error_0_1.hpp>

#include <cetl/cetl.hpp>

#include <spdlog/fmt/fmt.h>
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
inline sdk::Error errnoToError(const int errno_value) noexcept
{
    using sdk::Error;

    CETL_DEBUG_ASSERT(errno_value > 0, "Positive Posix error number is expected.");

    switch (errno_value)
    {
    case EBUSY:
        return Error{Error::Code::Busy, errno_value};
    case ENOENT:
        return Error{Error::Code::NoEntry, errno_value};
    case ETIMEDOUT:
        return Error{Error::Code::TimedOut, errno_value};
    case ENOMEM:
        return Error{Error::Code::OutOfMemory, errno_value};
    case EEXIST:
        return Error{Error::Code::AlreadyExists, errno_value};
    case EINVAL:
        return Error{Error::Code::InvalidArgument, errno_value};
    case ECANCELED:
        return Error{Error::Code::Canceled, errno_value};
    case ENOTCONN:
        return Error{Error::Code::NotConnected, errno_value};
    case ECONNRESET:
        return Error{Error::Code::Disconnected, errno_value};
    case ESHUTDOWN:
        return Error{Error::Code::Shutdown, errno_value};
    case EINPROGRESS:
        return Error{Error::Code::OperationInProgress, errno_value};
    default:
        return Error{Error::Code::Other, errno_value};
    }
}

/// Converts a raw integer to an optional error code.
///
/// `0` is converted to `nullopt`.
/// Internally in use to convert raw integers (from DSDL types) to optional error codes.
///
inline sdk::OptError dsdlErrorToOptError(const Error_0_1& dsdl_error) noexcept
{
    if (dsdl_error.error_code == 0)
    {
        return sdk::OptError{};
    }
    const auto error_code = static_cast<sdk::Error::Code>(dsdl_error.error_code);

    if (dsdl_error.raw_errno == 0)
    {
        return sdk::Error{error_code};
    }

    return sdk::Error{error_code, dsdl_error.raw_errno};
}

/// Converts an optional SDK error to its underlying raw integers.
///
/// `nullopt` is converted to `0`.
/// Internally in use to convert optional error codes to raw integers (for DSDL types).
///
inline void optErrorToDsdlError(const sdk::OptError opt_error, Error_0_1& dsdl_error) noexcept
{
    if (const auto error = opt_error)
    {
        dsdl_error.error_code = static_cast<std::underlying_type_t<sdk::Error::Code>>(error->getCode());
        dsdl_error.raw_errno  = error->getOptErrno().value_or(0);
        return;
    }

    dsdl_error.error_code = 0;
    dsdl_error.raw_errno  = 0;
}

}  // namespace common
}  // namespace ocvsmd

// MARK: - Formatting

// NOLINTBEGIN

template <>
struct fmt::formatter<ocvsmd::sdk::Error> : formatter<std::string>
{
    static auto format(const ocvsmd::sdk::Error error, format_context& ctx)
    {
        using ocvsmd::sdk::Error;

        const auto* error_name = "ErrorCode";
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
        }

        if (const auto opt_errno = error.getOptErrno())
        {
            return format_to(ctx.out(), "{}(errno={})", error_name, *opt_errno);
        }
        return format_to(ctx.out(), "{}", error_name);
    }
};
template <>
struct fmt::formatter<ocvsmd::sdk::OptError> : formatter<std::string>
{
    static auto format(const ocvsmd::sdk::OptError opt_error, format_context& ctx)
    {
        return opt_error.has_value() ? formatter<ocvsmd::sdk::Error>::format(*opt_error, ctx)
                                     : format_to(ctx.out(), "null");
    }
};

template <>
struct fmt::formatter<ocvsmd::common::ipc::AnyChannel::Connected> : formatter<std::string>
{
    auto format(ocvsmd::common::ipc::AnyChannel::Connected, format_context& ctx) const
    {
        return format_to(ctx.out(), "Connected");
    }
};

template <>
struct fmt::formatter<ocvsmd::common::ipc::AnyChannel::Completed> : formatter<std::string>
{
    auto format(ocvsmd::common::ipc::AnyChannel::Completed completed, format_context& ctx) const
    {
        if (const auto opt_error = completed.opt_error)
        {
            return format_to(ctx.out(), "Completed(alive={}, err={})", completed.keep_alive, *opt_error);
        }
        return format_to(ctx.out(), "Completed(alive={})", completed.keep_alive);
    }
};

// NOLINTEND

#endif  // OCVSMD_COMMON_HELPERS_HPP_INCLUDED
