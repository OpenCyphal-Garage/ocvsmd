//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IO_GTEST_HELPERS_HPP_INCLUDED
#define OCVSMD_COMMON_IO_GTEST_HELPERS_HPP_INCLUDED

#include "common/common_gtest_helpers.hpp"
#include "common_helpers.hpp"
#include "dsdl_helpers.hpp"
#include "io/socket_buffer.hpp"

#include "ocvsmd/sdk/defines.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest-matchers.h>
#include <gtest/gtest-printers.h>

#include <cstdint>
#include <ios>
#include <ostream>
#include <vector>

namespace ocvsmd
{
namespace common
{
namespace io
{

// MARK: - GTest Matchers:

template <typename Msg>
class PayloadMatcher
{
public:
    PayloadMatcher(cetl::pmr::memory_resource& memory, testing::Matcher<const Msg&> matcher)
        : memory_{memory}
        , matcher_(std::move(matcher))
    {
    }

    bool MatchAndExplain(const Payload& payload, testing::MatchResultListener* listener) const
    {
        Msg        msg{&memory_};
        const auto result = tryDeserializePayload<Msg>(payload, msg);
        if (!result)
        {
            if (listener->IsInterested())
            {
                *listener << "Failed to deserialize the payload.";
            }
            return false;
        }

        const bool match = matcher_.MatchAndExplain(msg, listener);
        if (!match && listener->IsInterested())
        {
            *listener << ".\n          Payload: ";
            *listener << testing::PrintToString(msg);
        }
        return match;
    }

    bool MatchAndExplain(const SocketBuffer& sock_buff, testing::MatchResultListener* listener) const
    {
        std::vector<cetl::byte> flatten;
        for (const auto payload : sock_buff.listFragments())
        {
            flatten.insert(flatten.end(), payload.begin(), payload.end());
        }
        return MatchAndExplain({flatten.data(), flatten.size()}, listener);
    }

    void DescribeTo(std::ostream* os) const
    {
        *os << "is a value of type '" << "GetTypeName()" << "' and the value ";
        matcher_.DescribeTo(os);
    }

    void DescribeNegationTo(std::ostream* os) const
    {
        *os << "is a value of type other than '" << "GetTypeName()" << "' or the value ";
        matcher_.DescribeNegationTo(os);
    }

private:
    cetl::pmr::memory_resource&        memory_;
    const testing::Matcher<const Msg&> matcher_;

};  // PayloadMatcher

template <typename Msg>
class PayloadVariantMatcher
{
public:
    PayloadVariantMatcher(cetl::pmr::memory_resource&                        memory,
                          testing::Matcher<const typename Msg::VariantType&> matcher)
        : memory_{memory}
        , matcher_(std::move(matcher))
    {
    }

    bool MatchAndExplain(const Payload& payload, testing::MatchResultListener* listener) const
    {
        Msg        msg{&memory_};
        const auto result = tryDeserializePayload<Msg>(payload, msg);
        if (!result)
        {
            if (listener->IsInterested())
            {
                *listener << "Failed to deserialize the payload.";
            }
            return false;
        }

        const bool match = matcher_.MatchAndExplain(msg.union_value, listener);
        if (!match && listener->IsInterested())
        {
            *listener << ".\n          Payload: ";
            *listener << testing::PrintToString(msg);
        }
        return match;
    }

    bool MatchAndExplain(const SocketBuffer& sock_buff, testing::MatchResultListener* listener) const
    {
        std::vector<cetl::byte> flatten;
        for (const auto payload : sock_buff.listFragments())
        {
            flatten.insert(flatten.end(), payload.begin(), payload.end());
        }
        return MatchAndExplain({flatten.data(), flatten.size()}, listener);
    }

    void DescribeTo(std::ostream* os) const
    {
        *os << "is a variant<> with value of type '" << "GetTypeName()" << "' and the value ";
        matcher_.DescribeTo(os);
    }

    void DescribeNegationTo(std::ostream* os) const
    {
        *os << "is a variant<> with value of type other than '" << "GetTypeName()" << "' or the value ";
        matcher_.DescribeNegationTo(os);
    }

private:
    cetl::pmr::memory_resource&                              memory_;
    const testing::Matcher<const typename Msg::VariantType&> matcher_;

};  // PayloadVariantMatcher

template <typename Msg>
testing::PolymorphicMatcher<PayloadMatcher<Msg>> PayloadWith(cetl::pmr::memory_resource&         mr,
                                                             const testing::Matcher<const Msg&>& payload_matcher)
{
    return testing::MakePolymorphicMatcher(PayloadMatcher<Msg>(mr, payload_matcher));
}

template <typename Msg>
testing::PolymorphicMatcher<PayloadVariantMatcher<Msg>> PayloadVariantWith(
    cetl::pmr::memory_resource&                               mr,
    const testing::Matcher<const typename Msg::VariantType&>& matcher)
{
    return testing::MakePolymorphicMatcher(PayloadVariantMatcher<Msg>(mr, matcher));
}

}  // namespace io
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IO_GTEST_HELPERS_HPP_INCLUDED
