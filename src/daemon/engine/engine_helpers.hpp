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

inline sdk::Error cyErrorToSdkError(const libcyphal::MemoryError) noexcept
{
    return sdk::Error{sdk::Error::Code::OutOfMemory};
}
inline sdk::Error cyErrorToSdkError(const libcyphal::transport::CapacityError) noexcept
{
    return sdk::Error{sdk::Error::Code::OutOfMemory};
}

inline sdk::Error cyErrorToSdkError(const libcyphal::ArgumentError) noexcept
{
    return sdk::Error{sdk::Error::Code::InvalidArgument};
}
inline sdk::Error cyErrorToSdkError(const libcyphal::transport::AnonymousError) noexcept
{
    return sdk::Error{sdk::Error::Code::InvalidArgument};
}
inline sdk::Error cyErrorToSdkError(const nunavut::support::Error) noexcept
{
    return sdk::Error{sdk::Error::Code::InvalidArgument};
}

inline sdk::Error cyErrorToSdkError(const libcyphal::transport::AlreadyExistsError) noexcept
{
    return sdk::Error{sdk::Error::Code::AlreadyExists};
}

inline sdk::Error cyErrorToSdkError(const libcyphal::transport::PlatformError& platform_error) noexcept
{
    return common::errnoToError(platform_error->code());
}

inline sdk::Error cyErrorToSdkError(const libcyphal::presentation::ResponsePromiseExpired) noexcept
{
    return sdk::Error{sdk::Error::Code::TimedOut};
}

inline sdk::Error cyErrorToSdkError(
    const libcyphal::presentation::detail::ClientBase::TooManyPendingRequestsError) noexcept
{
    return sdk::Error{sdk::Error::Code::Busy};
}

template <typename Variant>
sdk::OptError cyFailureToOptError(const Variant& cy_failure)
{
    return sdk::OptError{cetl::visit([](const auto& cy_error) { return cyErrorToSdkError(cy_error); }, cy_failure)};
}

}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_HELPERS_HPP_INCLUDED
