//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_VERIFY_UTILZ_HPP_INCLUDED
#define OCVSMD_VERIFY_UTILZ_HPP_INCLUDED

#include <cetl/pf17/cetlpf.hpp>

#include <cstdint>

namespace ocvsmd
{
namespace verify_utilz
{

constexpr cetl::byte b(const std::uint8_t b)
{
    return static_cast<cetl::byte>(b);
}

}  // namespace verify_utilz
}  // namespace ocvsmd

#endif  // OCVSMD_VERIFY_UTILZ_HPP_INCLUDED
