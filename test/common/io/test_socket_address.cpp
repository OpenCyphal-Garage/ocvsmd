//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "io/socket_address.hpp"

#include "ocvsmd/sdk/defines.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>

namespace
{

using namespace ocvsmd::common::io;  // NOLINT This our main concern here in the unit tests.
using ocvsmd::sdk::Error;

using testing::_;
using testing::VariantWith;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

class TestSocketAddress : public testing::Test
{};

// MARK: - Tests:

TEST_F(TestSocketAddress, parse_unix_domain)
{
    using Result = SocketAddress::ParseResult;

    {
        const std::string test_path         = "/tmp/ocvsmd.sock";
        auto              maybe_socket_addr = SocketAddress::parse("unix:" + test_path, 0);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto raw_address_and_len = socket_address.getRaw();

        const auto* const addr_un = reinterpret_cast<const sockaddr_un*>(raw_address_and_len.first);  // NOLINT
        EXPECT_TRUE(socket_address.isUnix());
        EXPECT_FALSE(socket_address.isAnyInet());
        EXPECT_THAT(addr_un->sun_family, AF_UNIX);
        EXPECT_THAT(addr_un->sun_path, test_path);
    }

    // try max possible path length
    constexpr auto MaxPath = sizeof(sockaddr_un::sun_path);
    {
        const std::string max_path(MaxPath - 1, 'x');
        auto              maybe_socket_addr = SocketAddress::parse("unix:" + max_path, 0);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto raw_address_and_len = socket_address.getRaw();

        const auto* const addr_un = reinterpret_cast<const sockaddr_un*>(raw_address_and_len.first);  // NOLINT
        EXPECT_THAT(addr_un->sun_family, AF_UNIX);
        EXPECT_THAT(addr_un->sun_path, max_path);
    }

    // try beyond max possible path length
    {
        const std::string too_long_path(MaxPath, 'x');
        auto              maybe_socket_addr = SocketAddress::parse("unix:" + too_long_path, 0);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Failure>(Result::Failure{Error::Code::InvalidArgument}));
    }
}

TEST_F(TestSocketAddress, parse_abstract_unix_domain)
{
    using Result = SocketAddress::ParseResult;

    {
        const std::string test_path         = "com.example.ocvsmd";
        auto              maybe_socket_addr = SocketAddress::parse("unix-abstract:" + test_path, 0);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto              socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto              raw_address_and_len = socket_address.getRaw();
        const auto* const addr_un = reinterpret_cast<const sockaddr_un*>(raw_address_and_len.first);  // NOLINT
        EXPECT_TRUE(socket_address.isUnix());
        EXPECT_FALSE(socket_address.isAnyInet());
        EXPECT_THAT(addr_un->sun_family, AF_UNIX);
        EXPECT_THAT(addr_un->sun_path[0], '\0');
        EXPECT_THAT(addr_un->sun_path + 1, test_path);  // NOLINT
    }

    // try with \0 in the path
    {
        const std::string test_path{"com\0example\0ocvsmd", 18};  // NOLINT
        auto              maybe_socket_addr = SocketAddress::parse("unix-abstract:" + test_path, 0);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto              socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto              raw_address_and_len = socket_address.getRaw();
        const auto* const addr_un = reinterpret_cast<const sockaddr_un*>(raw_address_and_len.first);  // NOLINT
        EXPECT_THAT(addr_un->sun_family, AF_UNIX);
        EXPECT_THAT(addr_un->sun_path[0], '\0');
        EXPECT_TRUE(0 == memcmp(addr_un->sun_path + 1, test_path.data(), test_path.size() + 1));  // NOLINT
    }

    // try max possible path length
    constexpr auto MaxPath = sizeof(sockaddr_un::sun_path);
    {
        const std::string max_path(MaxPath - 2, 'x');
        auto              maybe_socket_addr = SocketAddress::parse("unix-abstract:" + max_path, 0);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto raw_address_and_len = socket_address.getRaw();

        const auto* const addr_un = reinterpret_cast<const sockaddr_un*>(raw_address_and_len.first);  // NOLINT
        EXPECT_THAT(addr_un->sun_family, AF_UNIX);
        EXPECT_THAT(addr_un->sun_path[0], '\0');
        EXPECT_THAT(addr_un->sun_path + 1, max_path);  // NOLINT
    }

    // try beyond max possible path length
    {
        const std::string too_long_path(MaxPath - 1, 'x');
        auto              maybe_socket_addr = SocketAddress::parse("unix-abstract:" + too_long_path, 0);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Failure>(Result::Failure{Error::Code::InvalidArgument}));
    }
}

TEST_F(TestSocketAddress, parse_ipv4)
{
    using Result = SocketAddress::ParseResult;

    {
        const std::string test_addr         = "tcp://127.0.0.1";
        auto              maybe_socket_addr = SocketAddress::parse(test_addr, 0x1234);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto              socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto              raw_address_and_len = socket_address.getRaw();
        const auto* const addr_in = reinterpret_cast<const sockaddr_in*>(raw_address_and_len.first);  // NOLINT
        EXPECT_FALSE(socket_address.isUnix());
        EXPECT_TRUE(socket_address.isAnyInet());
        EXPECT_THAT(addr_in->sin_family, AF_INET);
        EXPECT_THAT(ntohs(addr_in->sin_port), 0x1234);
        EXPECT_THAT(ntohl(addr_in->sin_addr.s_addr), 0x7F000001);
    }

    // try with port
    {
        const std::string test_addr         = "tcp://192.168.1.123:8080";
        auto              maybe_socket_addr = SocketAddress::parse(test_addr, 80);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto              socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto              raw_address_and_len = socket_address.getRaw();
        const auto* const addr_in = reinterpret_cast<const sockaddr_in*>(raw_address_and_len.first);  // NOLINT
        EXPECT_THAT(addr_in->sin_family, AF_INET);
        EXPECT_THAT(ntohs(addr_in->sin_port), 8080);
        EXPECT_THAT(ntohl(addr_in->sin_addr.s_addr), 0xC0A8017B);
    }

    // try invalid ip
    {
        const std::string test_addr         = "tcp://127.0.0.256";
        auto              maybe_socket_addr = SocketAddress::parse(test_addr, 80);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Failure>(Result::Failure{Error::Code::InvalidArgument}));
    }

    // try unsupported
    {
        const std::string test_addr         = "tcp://localhost";
        auto              maybe_socket_addr = SocketAddress::parse(test_addr, 80);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Failure>(Result::Failure{Error::Code::InvalidArgument}));
    }
}

TEST_F(TestSocketAddress, parse_ipv6)
{
    using Result = SocketAddress::ParseResult;

    {
        const std::string test_addr         = "tcp://::1";
        auto              maybe_socket_addr = SocketAddress::parse(test_addr, 0x1234);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto              socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto              raw_address_and_len = socket_address.getRaw();
        const auto* const addr_in6 = reinterpret_cast<const sockaddr_in6*>(raw_address_and_len.first);  // NOLINT
        EXPECT_FALSE(socket_address.isUnix());
        EXPECT_TRUE(socket_address.isAnyInet());
        EXPECT_THAT(addr_in6->sin6_family, AF_INET6);
        EXPECT_THAT(ntohs(addr_in6->sin6_port), 0x1234);
        EXPECT_THAT(addr_in6->sin6_addr.s6_addr,  //
                    testing::ElementsAre(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1));
    }

    // try with port
    {
        const std::string test_addr         = "tcp://[2001:db8::1]:8080";
        auto              maybe_socket_addr = SocketAddress::parse(test_addr, 0);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto              socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto              raw_address_and_len = socket_address.getRaw();
        const auto* const addr_in6 = reinterpret_cast<const sockaddr_in6*>(raw_address_and_len.first);  // NOLINT
        EXPECT_THAT(addr_in6->sin6_family, AF_INET6);
        EXPECT_THAT(ntohs(addr_in6->sin6_port), 8080);
        EXPECT_THAT(addr_in6->sin6_addr.s6_addr,  //
                    testing::ElementsAre(0x20, 0x01, 0x0D, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1));
    }

    // try invalid
    {
        // missing closing bracket
        ASSERT_THAT(SocketAddress::parse("tcp://[::1", 0),
                    VariantWith<Result::Failure>(Result::Failure{Error::Code::InvalidArgument}));

        // missing colon after bracket
        ASSERT_THAT(SocketAddress::parse("tcp://[::1]8080", 0),
                    VariantWith<Result::Failure>(Result::Failure{Error::Code::InvalidArgument}));

        // invalid port number
        ASSERT_THAT(SocketAddress::parse("tcp://[::1]:80_80", 0),
                    VariantWith<Result::Failure>(Result::Failure{Error::Code::InvalidArgument}));

        // too big port number
        ASSERT_THAT(SocketAddress::parse("tcp://[::1]:65536", 0),
                    VariantWith<Result::Failure>(Result::Failure{Error::Code::InvalidArgument}));
    }
}

TEST_F(TestSocketAddress, parse_wildcard)
{
    using Result = SocketAddress::ParseResult;

    {
        const std::string test_addr         = "tcp://*";
        auto              maybe_socket_addr = SocketAddress::parse(test_addr, 0x1234);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto              socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto              raw_address_and_len = socket_address.getRaw();
        const auto* const addr_in6 = reinterpret_cast<const sockaddr_in6*>(raw_address_and_len.first);  // NOLINT
        EXPECT_FALSE(socket_address.isUnix());
        EXPECT_TRUE(socket_address.isAnyInet());
        EXPECT_THAT(addr_in6->sin6_family, AF_INET6);
        EXPECT_THAT(ntohs(addr_in6->sin6_port), 0x1234);
        EXPECT_THAT(addr_in6->sin6_addr.s6_addr,  //
                    testing::ElementsAre(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
    }

    // try with port
    {
        const std::string test_addr         = "tcp://*:8080";
        auto              maybe_socket_addr = SocketAddress::parse(test_addr, 0x1234);
        ASSERT_THAT(maybe_socket_addr, VariantWith<Result::Success>(_));
        auto              socket_address      = cetl::get<Result::Success>(maybe_socket_addr);
        auto              raw_address_and_len = socket_address.getRaw();
        const auto* const addr_in6 = reinterpret_cast<const sockaddr_in6*>(raw_address_and_len.first);  // NOLINT
        EXPECT_THAT(addr_in6->sin6_family, AF_INET6);
        EXPECT_THAT(ntohs(addr_in6->sin6_port), 8080);
        EXPECT_THAT(addr_in6->sin6_addr.s6_addr,  //
                    testing::ElementsAre(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
    }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

}  // namespace
