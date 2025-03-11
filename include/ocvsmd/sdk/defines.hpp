//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_DEFINES_HPP_INCLUDED
#define OCVSMD_SDK_DEFINES_HPP_INCLUDED

#include <cetl/pf20/cetlpf.hpp>

#include <cstdint>

namespace ocvsmd
{
namespace sdk
{

/// Defines the type of the Cyphal node ID.
///
using CyphalNodeId = std::uint16_t;

/// Defines span of the Cyphal node IDs.
///
using CyphalNodeIds = cetl::span<CyphalNodeId>;

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_DEFINES_HPP_INCLUDED
