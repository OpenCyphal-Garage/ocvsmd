//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_TYPES_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_TYPES_HPP_INCLUDED

#include <cetl/pf20/cetlpf.hpp>

#include <cstdint>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

using Payload  = cetl::span<const std::uint8_t>;
using Payloads = cetl::span<const Payload>;

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_TYPES_HPP_INCLUDED
