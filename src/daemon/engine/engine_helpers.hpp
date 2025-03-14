//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_HELPERS_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_HELPERS_HPP_INCLUDED

#include "common_helpers.hpp"
#include "ocvsmd/sdk/defines.hpp"

#include <nunavut/support/serialization.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/errors.hpp>
#include <libcyphal/presentation/client.hpp>
#include <libcyphal/presentation/response_promise.hpp>
#include <libcyphal/transport/errors.hpp>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{

inline sdk::ErrorCode errorToCode(const libcyphal::MemoryError) noexcept
{
    return sdk::ErrorCode::OutOfMemory;
}
inline sdk::ErrorCode errorToCode(const libcyphal::transport::CapacityError) noexcept
{
    return sdk::ErrorCode::OutOfMemory;
}

inline sdk::ErrorCode errorToCode(const libcyphal::ArgumentError) noexcept
{
    return sdk::ErrorCode::InvalidArgument;
}
inline sdk::ErrorCode errorToCode(const libcyphal::transport::AnonymousError) noexcept
{
    return sdk::ErrorCode::InvalidArgument;
}
inline sdk::ErrorCode errorToCode(const nunavut::support::Error) noexcept
{
    return sdk::ErrorCode::InvalidArgument;
}

inline sdk::ErrorCode errorToCode(const libcyphal::transport::AlreadyExistsError) noexcept
{
    return sdk::ErrorCode::AlreadyExists;
}

inline sdk::ErrorCode errorToCode(const libcyphal::transport::PlatformError& platform_error) noexcept
{
    return common::errnoToErrorCode(platform_error->code());
}

inline sdk::ErrorCode errorToCode(const libcyphal::presentation::ResponsePromiseExpired) noexcept
{
    return sdk::ErrorCode::TimedOut;
}

inline sdk::ErrorCode errorToCode(
    const libcyphal::presentation::detail::ClientBase::TooManyPendingRequestsError) noexcept
{
    return sdk::ErrorCode::Busy;
}

template <typename Variant>
sdk::OptErrorCode failureToOptErrorCode(const Variant& failure)
{
    return sdk::OptErrorCode{cetl::visit([](const auto& error) { return errorToCode(error); }, failure)};
}

}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_HELPERS_HPP_INCLUDED
