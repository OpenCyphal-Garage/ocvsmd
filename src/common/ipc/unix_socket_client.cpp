//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "unix_socket_client.hpp"

#include "dsdl_helpers.hpp"
#include "platform/posix_utils.hpp"

#include <nunavut/support/serialization.hpp>
#include <ocvsmd/common/dsdl/Foo_1_0.hpp>

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

UnixSocketClient::UnixSocketClient(cetl::pmr::memory_resource& memory, std::string socket_path)
    : memory_{memory}
    , socket_path_{std::move(socket_path)}
    , client_fd_{-1}
{
}

UnixSocketClient::~UnixSocketClient()
{
    if (client_fd_ != -1)
    {
        platform::posixSyscallError([this] {
            //
            return ::close(client_fd_);
        });
    }
}

bool UnixSocketClient::connect_to_server()
{
    CETL_DEBUG_ASSERT(client_fd_ == -1, "");

    if (const auto err = platform::posixSyscallError([this] {
            //
            return client_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        }))
    {
        std::cerr << "Failed to create socket: " << ::strerror(err) << "\n";
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family                        = AF_UNIX;
    const std::string abstract_socket_path = '\0' + socket_path_;
    CETL_DEBUG_ASSERT(abstract_socket_path.size() <= sizeof(addr.sun_path), "");
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    std::memcpy(addr.sun_path,
                abstract_socket_path.c_str(),
                std::min(sizeof(addr.sun_path), abstract_socket_path.size()));

    if (const auto err = platform::posixSyscallError([this, &addr, &abstract_socket_path] {
            //
            return ::connect(client_fd_,
                             // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                             reinterpret_cast<const sockaddr*>(&addr),
                             offsetof(struct sockaddr_un, sun_path) + abstract_socket_path.size());
        }))
    {
        std::cerr << "Failed to connect to server: " << ::strerror(err) << "\n";
        return false;
    }

    return true;
}

void UnixSocketClient::send_message(const dsdl::Foo_1_0& foo_message) const
{
    using Failure = cetl::variant<int, nunavut::support::Error>;

    tryPerformOnSerialized<dsdl::Foo_1_0,
                           Failure,
                           dsdl::Foo_1_0::_traits_::SerializationBufferSizeBytes,
                           true>(foo_message, memory_, [this](const cetl::span<const cetl::byte> msg_bytes) {
        //
        if (const auto err = platform::posixSyscallError([this, msg_bytes] {
                //
                return ::write(client_fd_, msg_bytes.data(), msg_bytes.size());
            }))
        {
            std::cerr << "Failed to write: " << ::strerror(err) << "\n";
        }
        return 0;
    });

    constexpr std::size_t      buf_size = 256;
    std::array<char, buf_size> buffer{};
    ssize_t                    bytes_read = 0;
    if (const auto err = platform::posixSyscallError([this, &bytes_read, &buffer] {
            //
            return bytes_read = ::read(client_fd_, buffer.data(), buffer.size() - 1);
        }))
    {
        std::cerr << "Failed to read: " << ::strerror(err) << "\n";
        return;
    }
    if (bytes_read > 0)
    {
        buffer[bytes_read] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        std::cout << "Received: " << buffer.data() << "\n";
    }
}

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd
