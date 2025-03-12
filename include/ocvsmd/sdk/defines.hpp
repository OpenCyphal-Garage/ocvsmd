//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_DEFINES_HPP_INCLUDED
#define OCVSMD_SDK_DEFINES_HPP_INCLUDED

#include <cetl/pf20/cetlpf.hpp>

#include <cerrno>
#include <cstdint>

namespace ocvsmd
{
namespace sdk
{

/// Defines some common error codes of IPC operations.
///
/// Maps to `errno` values, hence `std::int32_t` inheritance and zero on success.
///
enum class ErrorCode : std::int32_t  // NOLINT
{
    Success             = 0,
    Busy                = EBUSY,
    NoEntry             = ENOENT,
    TimedOut            = ETIMEDOUT,
    OutOfMemory         = ENOMEM,
    AlreadyExists       = EEXIST,
    InvalidArgument     = EINVAL,
    Canceled            = ECANCELED,
    NotConnected        = ENOTCONN,
    Disconnected        = ECONNRESET,
    Shutdown            = ESHUTDOWN,
    OperationInProgress = EINPROGRESS,

};  // ErrorCode

/// Defines the type of the Cyphal node ID.
///
using CyphalNodeId = std::uint16_t;

/// Defines span of the Cyphal node IDs.
///
using CyphalNodeIds = cetl::span<CyphalNodeId>;

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_DEFINES_HPP_INCLUDED
