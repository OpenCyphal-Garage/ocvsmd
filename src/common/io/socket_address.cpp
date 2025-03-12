//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "socket_address.hpp"

#include "io.hpp"
#include "logging.hpp"
#include "ocvsmd/platform/posix_utils.hpp"
#include "ocvsmd/sdk/defines.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <spdlog/fmt/fmt.h>

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace io
{

SocketAddress::SocketAddress() noexcept
    : is_wildcard_{false}
    , addr_len_{0}
    , addr_storage_{}
{
}

std::pair<const sockaddr*, socklen_t> SocketAddress::getRaw() const noexcept
{
    return {&asGenericAddr(), addr_len_};
}

std::uint16_t SocketAddress::getPort() const
{
    switch (asGenericAddr().sa_family)
    {
    case AF_INET:
        return ntohs(asInetAddr().sin_port);
    case AF_INET6:
        return ntohs(asInet6Addr().sin6_port);
    default:
        return 0;
    }
}

std::pair<std::string, std::string> SocketAddress::getUnixPrefixAndPath() const
{
    CETL_DEBUG_ASSERT(isUnix(), "");
    CETL_DEBUG_ASSERT(addr_len_ >= offsetof(sockaddr_un, sun_path), "");

    if (const auto path_len = addr_len_ - offsetof(sockaddr_un, sun_path))
    {
        const auto& addr_un = asUnixAddr();
        if (addr_un.sun_path[0] == '\0')
        {
            // NOLINTNEXTLINE(*-array-to-pointer-decay, *-no-array-decay, *-pointer-arithmetic)
            return {"unix-abstract:", std::string{&addr_un.sun_path[1], path_len - 1}};
        }

        // NOLINTNEXTLINE(*-array-to-pointer-decay, *-no-array-decay)
        return {"unix:", std::string{addr_un.sun_path, path_len}};
    }
    return {"unix:", ""};
}

std::string SocketAddress::toString() const
{
    if (is_wildcard_)
    {
        return fmt::format("*:{}", getPort());
    }

    switch (asGenericAddr().sa_family)
    {
    case AF_INET: {
        std::array<char, INET_ADDRSTRLEN> buf{};
        if (const auto* addr = ::inet_ntop(AF_INET, &asInetAddr().sin_addr, buf.data(), buf.size()))
        {
            return fmt::format("{}:{}", addr, getPort());
        }
        break;
    }
    case AF_INET6: {
        std::array<char, INET6_ADDRSTRLEN> buf{};
        if (const auto* addr = ::inet_ntop(AF_INET6, &asInet6Addr().sin6_addr, buf.data(), buf.size()))
        {
            return fmt::format("[{}]:{}", addr, getPort());
        }
        break;
    }
    case AF_UNIX: {
        const auto prefix_and_path = getUnixPrefixAndPath();
        return prefix_and_path.first + prefix_and_path.second;
    }
    default:
        break;
    }

    return fmt::format("<unknown>(family={})", asGenericAddr().sa_family);
}

SocketAddress::SocketResult::Var SocketAddress::socket(const int socket_type) const
{
    OwnFd out_fd;

    const auto& addr_generic = asGenericAddr();
    if (const int err = platform::posixSyscallError([this, socket_type, &addr_generic, &out_fd] {
            //
            const int fd = ::socket(addr_generic.sa_family, socket_type, 0);
            if (fd != -1)
            {
                out_fd = OwnFd{fd};
            }
            return fd;
        }))
    {
        getLogger("io")->error("Failed to create socket: {}.", std::strerror(err));
        return static_cast<sdk::ErrorCode>(err);
    }

    if (const int err = platform::posixSyscallError([&out_fd] {
            //
            // NOLINTNEXTLINE(*-vararg)
            return ::fcntl(out_fd.get(), F_SETFL, O_NONBLOCK);
        }))
    {
        getLogger("io")->error("Failed to fcntl(O_NONBLOCK) socket: {}.", std::strerror(err));
        return static_cast<sdk::ErrorCode>(err);
    }

    // Disable Nagle's algorithm for TCP sockets, so that our small IPC packets are sent immediately.
    //
    if ((SOCK_STREAM == socket_type) && isAnyInet())
    {
        configureNoDelay(out_fd);
    }

    return out_fd;
}

sdk::ErrorCode SocketAddress::bind(const OwnFd& socket_fd) const
{
    const int raw_fd = socket_fd.get();
    CETL_DEBUG_ASSERT(raw_fd != -1, "");

    // Disable IPv6-only mode for dual-stack sockets (aka wildcard).
    if (is_wildcard_)
    {
        if (const int err = platform::posixSyscallError([raw_fd] {
                //
                constexpr int disable = 0;
                return ::setsockopt(raw_fd, IPPROTO_IPV6, IPV6_V6ONLY, &disable, sizeof(disable));
            }))
        {
            getLogger("io")->error("Failed to set IPV6_V6ONLY=0: {}.", std::strerror(err));
            return static_cast<sdk::ErrorCode>(err);
        }
    }

    const int err = platform::posixSyscallError([this, raw_fd] {
        //
        return ::bind(raw_fd, &asGenericAddr(), addr_len_);
    });
    if (err != 0)
    {
        getLogger("io")->error("Failed to bind socket: {}.", std::strerror(err));
    }
    return static_cast<sdk::ErrorCode>(err);
}

sdk::ErrorCode SocketAddress::connect(const OwnFd& socket_fd) const
{
    const int raw_fd = socket_fd.get();
    CETL_DEBUG_ASSERT(raw_fd != -1, "");

    const int err = platform::posixSyscallError([this, raw_fd] {
        //
        return ::connect(raw_fd, &asGenericAddr(), addr_len_);
    });
    switch (err)
    {
    case 0:
    case EINPROGRESS: {
        return sdk::ErrorCode::Success;
    }
    default: {
        getLogger("io")->error("Failed to connect to server: {}.", std::strerror(err));
        return static_cast<sdk::ErrorCode>(err);
    }
    }
}

cetl::optional<OwnFd> SocketAddress::accept(const OwnFd& server_fd)
{
    CETL_DEBUG_ASSERT(server_fd.get() != -1, "");

    while (true)
    {
        addr_len_ = sizeof(addr_storage_);
        OwnFd client_fd{::accept(server_fd.get(), &asGenericAddr(), &addr_len_)};
        if (client_fd.get() >= 0)
        {
            if (const int err = platform::posixSyscallError([&client_fd] {
                    //
                    // NOLINTNEXTLINE(*-vararg)
                    return ::fcntl(client_fd.get(), F_SETFL, O_NONBLOCK);
                }))
            {
                getLogger("io")->warn("Failed to fcntl(O_NONBLOCK) accept socket: {}.", std::strerror(err));
                return cetl::nullopt;
            }

            // Disable Nagle's algorithm for TCP sockets, so that our small IPC packets are sent immediately.
            //
            if (isAnyInet())
            {
                configureNoDelay(client_fd);
            }

            return client_fd;
        }

        const int err = errno;
        switch (err)
        {
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
        {
            // Not ready yet - just exit.
            return cetl::nullopt;
        }

        // The list of errors below is a guess of temporary network errors (vs permanent ones).
        //
        case EINTR:
        case ENETDOWN:
        case ETIMEDOUT:
        case EHOSTDOWN:
        case ENETUNREACH:
        case ECONNABORTED:
        case EHOSTUNREACH:
#ifdef EPROTO
        case EPROTO:  // not defined on OpenBSD
#endif
        {
            // Just log and retry.
            getLogger("io")->debug("Failed to accept connection; retrying (fd={}, err={}).", server_fd.get(), err);
            break;
        }

        default: {
            // Just log and exit.
            getLogger("io")->warn("Failed to accept connection (fd={}, err={}): {}.",
                                  server_fd.get(),
                                  err,
                                  std::strerror(err));
            return cetl::nullopt;
        }
        }  // switch err

    }  // while(true)

    return cetl::nullopt;
}

/// Disables Nagle's algorithm for TCP sockets, so that our small IPC packets are sent immediately.
///
void SocketAddress::configureNoDelay(const OwnFd& fd)
{
    if (const int err = platform::posixSyscallError([&fd] {
            //
            constexpr int enable = 1;
            return ::setsockopt(fd.get(), IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
        }))
    {
        getLogger("io")->warn("Failed to set TCP_NODELAY=1 (fd={}, err={}): {}.", fd.get(), err, std::strerror(err));
    }
}

SocketAddress::ParseResult::Var SocketAddress::parse(const std::string& conn_str, const std::uint16_t port_hint)
{
    // Unix domain?
    //
    if (auto result = tryParseAsUnixDomain(conn_str))
    {
        return *result;
    }
    if (auto result = tryParseAsAbstractUnixDomain(conn_str))
    {
        return *result;
    }
    if (auto result = tryParseAsTcpAddress(conn_str, port_hint))
    {
        return *result;
    }

    getLogger("io")->error("Unsupported connection string format (conn_str='{}').", conn_str);
    return sdk::ErrorCode::InvalidArgument;
}

cetl::optional<SocketAddress::ParseResult::Var> SocketAddress::tryParseAsTcpAddress(const std::string&  conn_str,
                                                                                    const std::uint16_t port_hint)
{
    static const std::string tcp_prefix = "tcp://";
    if (0 != conn_str.compare(0, tcp_prefix.size(), tcp_prefix))
    {
        return cetl::nullopt;
    }
    const auto addr_str = conn_str.substr(tcp_prefix.size());

    // Extract the family, host, and port.
    //
    std::string   host;
    std::uint16_t port   = port_hint;
    const int     family = extractFamilyHostAndPort(addr_str, host, port);
    if (family == AF_UNSPEC)
    {
        return sdk::ErrorCode::InvalidArgument;
    }
    if (auto result = tryParseAsWildcard(host, port))
    {
        return *result;
    }

    // Convert the host string to inet address.
    //
    SocketAddress result{};
    void*         addr_target = nullptr;
    if (family == AF_INET6)
    {
        auto& result_inet6       = result.asInet6Addr();
        result.addr_len_         = sizeof(result_inet6);
        result_inet6.sin6_family = AF_INET6;
        result_inet6.sin6_port   = htons(port);
        addr_target              = &result_inet6.sin6_addr;
    }
    else
    {
        auto& result_inet4      = result.asInetAddr();
        result.addr_len_        = sizeof(result_inet4);
        result_inet4.sin_family = AF_INET;
        result_inet4.sin_port   = htons(port);
        addr_target             = &result_inet4.sin_addr;
    }
    const int convert_result = ::inet_pton(family, host.c_str(), addr_target);
    switch (convert_result)
    {
    case 1: {
        return result;
    }
    case 0: {
        getLogger("io")->error("Unsupported ip address format (addr='{}').", host);
        return sdk::ErrorCode::InvalidArgument;
    }
    default: {
        const int err = errno;
        getLogger("io")->error("Failed to parse address (addr='{}'): {}", host, std::strerror(err));
        return static_cast<sdk::ErrorCode>(err);
    }
    }
}

cetl::optional<SocketAddress::ParseResult::Var> SocketAddress::tryParseAsUnixDomain(const std::string& conn_str)
{
    static const std::string unix_prefix = "unix:";
    if (0 != conn_str.compare(0, unix_prefix.size(), unix_prefix))
    {
        return cetl::nullopt;
    }
    const auto path = conn_str.substr(unix_prefix.size());

    SocketAddress result{};
    auto&         result_un = result.asUnixAddr();
    result_un.sun_family    = AF_UNIX;

    // Reserve one byte for the null terminator.
    if ((path.size() + 1) > sizeof(result_un.sun_path))
    {
        getLogger("io")->error("Unix domain path is too long (path='{}').", conn_str);
        return sdk::ErrorCode::InvalidArgument;
    }

    // NOLINTNEXTLINE(*-array-to-pointer-decay, *-no-array-decay)
    std::strncpy(result_un.sun_path, path.c_str(), sizeof(result_un.sun_path));

    result.addr_len_ = offsetof(sockaddr_un, sun_path) + path.size() + 1;
    return result;
}

cetl::optional<SocketAddress::ParseResult::Var> SocketAddress::tryParseAsAbstractUnixDomain(const std::string& conn_str)
{
    static const std::string unix_prefix = "unix-abstract:";
    if (0 != conn_str.compare(0, unix_prefix.size(), unix_prefix))
    {
        return cetl::nullopt;
    }
    const auto path = conn_str.substr(unix_prefix.size());

    SocketAddress result{};
    auto&         result_un = result.asUnixAddr();
    result_un.sun_family    = AF_UNIX;

    // Reserve +1 byte for the null terminator. Not required for abstract domain but it is harmless.
    if ((path.size() + 1) > (sizeof(result_un.sun_path) - 1))  // `-1` b/c path starts at `[1]` (see `memcpy` below).
    {
        getLogger("io")->error("Unix domain path is too long (path='{}').", conn_str);
        return sdk::ErrorCode::InvalidArgument;
    }

    result_un.sun_path[0] = '\0';
    // `memcpy` (instead of `strcpy`) b/c `path` is allowed to contain null characters.
    // NOLINTNEXTLINE(*-array-to-pointer-decay, *-no-array-decay, *-pointer-arithmetic)
    std::memcpy(&result_un.sun_path[1], path.c_str(), path.size() + 1);

    result.addr_len_ = offsetof(sockaddr_un, sun_path) + path.size() + 1;  // include prefix null byte
    return result;
}

int SocketAddress::extractFamilyHostAndPort(const std::string& str, std::string& host, std::uint16_t& port)
{
    int         family = AF_INET;
    std::string port_part;

    if (0 == str.find_first_of('['))
    {
        // IPv6 starts with a bracket when with a port.
        family = AF_INET6;

        const auto end_bracket_pos = str.find_last_of(']');
        if (end_bracket_pos == std::string::npos)
        {
            getLogger("io")->error("Invalid IPv6 address; unclosed '[' (addr='{}').", str);
            return AF_UNSPEC;
        }
        host = str.substr(1, end_bracket_pos - 1);

        if (str.size() > end_bracket_pos + 1)
        {
            const auto expected_colon_pos = end_bracket_pos + 1;
            if (str[expected_colon_pos] != ':')
            {
                getLogger("io")->error("Invalid IPv6 address; expected port suffix after ']': (addr='{}').", str);
                return AF_UNSPEC;
            }
            port_part = str.substr(end_bracket_pos + 2);
        }
    }
    else
    {
        const auto colon_pos = str.find_first_of(':');
        if (colon_pos != std::string::npos)
        {
            if (str.find_first_of(':', colon_pos + 1) != std::string::npos)
            {
                // There are at least two colons, so it must be an IPv6 address (without port).
                family = AF_INET6;
                host   = str;
            }
            else
            {
                // There is only one colon (and no brackets), so it must be an IPv4 address with a port.
                host      = str.substr(0, colon_pos);
                port_part = str.substr(colon_pos + 1);
            }
        }
        else
        {
            // There is no colon in the string, so it must be an IPv4 address (without port).
            host = str;
        }
    }

    // Parse the port if any; otherwise keep untouched (hint).
    //
    if (!port_part.empty())
    {
        char*               end_ptr    = nullptr;
        const std::uint64_t maybe_port = std::strtoull(port_part.c_str(), &end_ptr, 0);
        if (*end_ptr != '\0')
        {
            getLogger("io")->error("Invalid port number (port='{}').", port_part);
            return AF_UNSPEC;
        }
        if (maybe_port > std::numeric_limits<std::uint16_t>::max())
        {
            getLogger("io")->error("Port number is too large (port={}).", maybe_port);
            return AF_UNSPEC;
        }
        port = static_cast<std::uint16_t>(maybe_port);
    }

    return family;
}

cetl::optional<SocketAddress::ParseResult::Success> SocketAddress::tryParseAsWildcard(const std::string&  host,
                                                                                      const std::uint16_t port)
{
    if (host != "*")
    {
        return cetl::nullopt;
    }

    SocketAddress result{};
    result.is_wildcard_    = true;
    auto& result_inet6     = result.asInet6Addr();
    result_inet6.sin6_port = htons(port);
    result.addr_len_       = sizeof(result_inet6);

    // IPv4 will be also enabled by IPV6_V6ONLY=0 (at `bind` method).
    result_inet6.sin6_family = AF_INET6;

    return result;
}

}  // namespace io
}  // namespace common
}  // namespace ocvsmd
