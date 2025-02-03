//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_NET_SOCKET_ADDRESS_HPP_INCLUDED
#define OCVSMD_COMMON_NET_SOCKET_ADDRESS_HPP_INCLUDED

#include "io.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <cstdint>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace io
{

class SocketAddress
{
public:
    OwnFd socket() const;

    struct ParseResult
    {
        using Failure = int;  // aka errno
        using Success = SocketAddress;
        using Var     = cetl::variant<Success, Failure>;
    };
    static ParseResult::Var parse(const std::string& str, const std::uint16_t port_hint);

    std::pair<const sockaddr*, socklen_t> getRaw() const noexcept;

    struct SocketResult
    {
        using Failure = int;  // aka errno
        using Success = OwnFd;
        using Var     = cetl::variant<Success, Failure>;
    };
    SocketResult::Var socket(const int type) const;

    int bind(const OwnFd& socket_fd) const;

private:
    SocketAddress() noexcept;

    static cetl::optional<ParseResult::Var> tryParseAsUnixDomain(const std::string& str);
    static cetl::optional<ParseResult::Var> tryParseAsAbstractUnixDomain(const std::string& str);
    static int extractFamilyHostAndPort(const std::string& str, std::string& host, std::uint16_t& port);
    static cetl::optional<ParseResult::Success> tryParseAsWildcard(const std::string& host, const std::uint16_t port);

    sockaddr& asGenericAddr()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<sockaddr&>(addr_storage_);
    }
    const sockaddr& asGenericAddr() const
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<const sockaddr&>(addr_storage_);
    }
    sockaddr_un& asUnixAddr()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<sockaddr_un&>(addr_storage_);
    }
    sockaddr_in& asInetAddr()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<sockaddr_in&>(addr_storage_);
    }
    sockaddr_in6& asInet6Addr()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<sockaddr_in6&>(addr_storage_);
    }

    bool             is_wildcard_;
    socklen_t        addr_len_;
    sockaddr_storage addr_storage_;

};  // SocketAddress

}  // namespace io
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_NET_SOCKET_ADDRESS_HPP_INCLUDED
