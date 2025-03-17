//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_GTEST_HELPERS_HPP_INCLUDED
#define OCVSMD_COMMON_GTEST_HELPERS_HPP_INCLUDED

#include <ocvsmd/common/Error_0_1.hpp>

#include <ostream>

namespace ocvsmd
{
namespace common
{

// MARK: - GTest Printers:

inline void PrintTo(const Error_0_1& error, std::ostream* os)
{
    *os << "Error_0_1{code=" << error.error_code << ", errno=" << error.raw_errno << "}";
}

// MARK: - Equitable-s for matching:

inline bool operator==(const Error_0_1& lhs, const Error_0_1& rhs)
{
    return (lhs.error_code == rhs.error_code) && (lhs.raw_errno == rhs.raw_errno);
}

}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_GTEST_HELPERS_HPP_INCLUDED
