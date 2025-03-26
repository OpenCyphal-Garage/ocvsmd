//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_CLI_DSDL_HELPERS_HPP_INCLUDED
#define OCVSMD_CLI_DSDL_HELPERS_HPP_INCLUDED

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <cstdint>

template <typename Message>
CETL_NODISCARD auto tryDeserializePayload(const cetl::span<const cetl::byte> payload, Message& out_message)
{
    // No lint b/c of integration with Nunavut.
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    return deserialize(out_message, {reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()});
}

#endif  // OCVSMD_CLI_DSDL_HELPERS_HPP_INCLUDED
