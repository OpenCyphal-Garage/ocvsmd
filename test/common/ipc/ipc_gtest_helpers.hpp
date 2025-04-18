//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_GTEST_HELPERS_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_GTEST_HELPERS_HPP_INCLUDED

#include "common/common_gtest_helpers.hpp"
#include "common/io/io_gtest_helpers.hpp"
#include "common_helpers.hpp"
#include "dsdl_helpers.hpp"

#include "ocvsmd/common/ipc/RouteChannelEnd_0_2.hpp"
#include "ocvsmd/common/ipc/RouteChannelMsg_0_1.hpp"
#include "ocvsmd/common/ipc/RouteConnect_0_1.hpp"
#include "ocvsmd/common/ipc/Route_0_2.hpp"
#include "ocvsmd/sdk/defines.hpp"

#include <uavcan/node/Version_1_0.hpp>
#include <uavcan/primitive/Empty_1_0.hpp>

#include <cetl/pf17/cetlpf.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest-matchers.h>
#include <gtest/gtest-printers.h>

#include <cstdint>
#include <ios>
#include <ostream>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

namespace ocvsmd
{
namespace common
{
namespace ipc
{

// MARK: - GTest Printers:

inline void PrintTo(const uavcan::primitive::Empty_1_0&, std::ostream* os)
{
    *os << "Empty_1_0";
}

inline void PrintTo(const uavcan::node::Version_1_0& ver, std::ostream* os)
{
    *os << "Version_1_0{'" << static_cast<int>(ver.major) << "." << static_cast<int>(ver.minor) << "'}";
}

inline void PrintTo(const RouteConnect_0_1& conn, std::ostream* os)
{
    *os << "RouteConnect_0_1{ver=";
    PrintTo(conn.version, os);
    *os << "}";
}

inline void PrintTo(const RouteChannelMsg_0_1& msg, std::ostream* os)
{
    *os << "RouteChannelMsg_1_0{tag=" << msg.tag << ", seq=" << msg.sequence << ", srv=0x" << std::hex << msg.service_id
        << ", payload_size=" << msg.payload_size << "}";
}

inline void PrintTo(const RouteChannelEnd_0_2& msg, std::ostream* os)
{
    *os << "RouteChannelEnd_0_2{tag=" << msg.tag << ", keep_alive=" << msg.keep_alive << ", err=";
    PrintTo(msg._error, os);
    *os << "}";
}

inline void PrintTo(const Route_0_2& route, std::ostream* os)
{
    *os << "Route_0_2{";
    cetl::visit([os](const auto& v) { PrintTo(v, os); }, route.union_value);
    *os << "}";
}

// MARK: - Equitable-s for matching:

inline bool operator==(const RouteConnect_0_1& lhs, const RouteConnect_0_1& rhs)
{
    return (lhs.version.major == rhs.version.major) && (lhs.version.minor == rhs.version.minor);
}

inline bool operator==(const RouteChannelMsg_0_1& lhs, const RouteChannelMsg_0_1& rhs)
{
    return (lhs.tag == rhs.tag) && (lhs.sequence == rhs.sequence) && (lhs.service_id == rhs.service_id) &&
           (lhs.payload_size == rhs.payload_size);
}

inline bool operator==(const RouteChannelEnd_0_2& lhs, const RouteChannelEnd_0_2& rhs)
{
    return (lhs.tag == rhs.tag) && (lhs._error == rhs._error) && (lhs.keep_alive == rhs.keep_alive);
}

// MARK: - GTest Matchers:

inline auto PayloadOfRouteConnect(cetl::pmr::memory_resource& mr,
                                  const std::uint8_t          ver_major = VERSION_MAJOR,
                                  const std::uint8_t          ver_minor = VERSION_MINOR,
                                  const sdk::OptError         opt_error = {})
{
    RouteConnect_0_1 route_conn{&mr};
    route_conn.version = {ver_major, ver_minor, &mr};
    optErrorToDsdlError(opt_error, route_conn._error);
    return io::PayloadVariantWith<Route_0_2>(mr, testing::VariantWith<RouteConnect_0_1>(route_conn));
}

template <typename Msg>
auto PayloadOfRouteChannelMsg(const Msg&                  msg,
                              cetl::pmr::memory_resource& mr,
                              const std::uint64_t         tag,
                              const std::uint64_t         seq,
                              const cetl::string_view     srv_name = "")
{
    RouteChannelMsg_0_1 route_ch_msg{tag, seq, AnyChannel::getServiceDesc<Msg>(srv_name).id, 0, &mr};
    EXPECT_THAT(tryPerformOnSerialized(  //
                    msg,
                    [&route_ch_msg](const auto payload) {
                        //
                        route_ch_msg.payload_size = payload.size();
                        return ocvsmd::sdk::OptError{};
                    }),
                ocvsmd::sdk::OptError{});
    return io::PayloadVariantWith<Route_0_2>(mr, testing::VariantWith<RouteChannelMsg_0_1>(route_ch_msg));
}

inline auto PayloadOfRouteChannelEnd(cetl::pmr::memory_resource& mr,  //
                                     const std::uint64_t         tag,
                                     const sdk::OptError         opt_error  = {},
                                     const bool                  keep_alive = false)
{
    RouteChannelEnd_0_2 ch_end{&mr};
    ch_end.tag        = tag;
    ch_end.keep_alive = keep_alive;
    optErrorToDsdlError(opt_error, ch_end._error);
    return io::PayloadVariantWith<Route_0_2>(mr, testing::VariantWith<RouteChannelEnd_0_2>(ch_end));
}

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

#endif  // OCVSMD_COMMON_IPC_GTEST_HELPERS_HPP_INCLUDED
