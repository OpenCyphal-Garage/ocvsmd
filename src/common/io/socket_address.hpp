//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_NET_SOCKET_ADDRESS_HPP_INCLUDED
#define OCVSMD_COMMON_NET_SOCKET_ADDRESS_HPP_INCLUDED

#include "io.hpp"
#include "ocvsmd/sdk/defines.hpp"

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

class SocketAddress final
{
public:
    struct ParseResult
    {
        using Failure = sdk::ErrorCode;
        using Success = SocketAddress;
        using Var     = cetl::variant<Success, Failure>;
    };
    static ParseResult::Var parse(const std::string& conn_str, const std::uint16_t port_hint);

    SocketAddress() noexcept;

    std::pair<const sockaddr*, socklen_t> getRaw() const noexcept;
    std::uint16_t                         getPort() const;

    bool isUnix() const noexcept
    {
        return asGenericAddr().sa_family == AF_UNIX;
    }

    bool isAnyInet() const noexcept
    {
        const auto family = asGenericAddr().sa_family;
        return (family == AF_INET) || (family == AF_INET6);
    }

    std::string toString() const;

    struct SocketResult
    {
        using Failure = sdk::ErrorCode;
        using Success = OwnFd;
        using Var     = cetl::variant<Success, Failure>;
    };
    SocketResult::Var socket(const int socket_type) const;

    sdk::ErrorCode        bind(const OwnFd& socket_fd) const;
    sdk::ErrorCode        connect(const OwnFd& socket_fd) const;
    cetl::optional<OwnFd> accept(const OwnFd& server_fd);

private:
    static void                             configureNoDelay(const OwnFd& fd);
    static cetl::optional<ParseResult::Var> tryParseAsUnixDomain(const std::string& conn_str);
    static cetl::optional<ParseResult::Var> tryParseAsAbstractUnixDomain(const std::string& conn_str);
    static cetl::optional<ParseResult::Var> tryParseAsTcpAddress(const std::string&  conn_str,
                                                                 const std::uint16_t port_hint);
    static int extractFamilyHostAndPort(const std::string& str, std::string& host, std::uint16_t& port);
    static cetl::optional<ParseResult::Success> tryParseAsWildcard(const std::string& host, const std::uint16_t port);

    std::pair<std::string, std::string> getUnixPrefixAndPath() const;

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
    const sockaddr_un& asUnixAddr() const
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<const sockaddr_un&>(addr_storage_);
    }
    sockaddr_in& asInetAddr()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<sockaddr_in&>(addr_storage_);
    }
    const sockaddr_in& asInetAddr() const
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<const sockaddr_in&>(addr_storage_);
    }
    sockaddr_in6& asInet6Addr()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<sockaddr_in6&>(addr_storage_);
    }
    const sockaddr_in6& asInet6Addr() const
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<const sockaddr_in6&>(addr_storage_);
    }

    bool             is_wildcard_;
    socklen_t        addr_len_;
    sockaddr_storage addr_storage_;

};  // SocketAddress

}  // namespace io
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_NET_SOCKET_ADDRESS_HPP_INCLUDED
