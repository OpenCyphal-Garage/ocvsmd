//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "socket_client.hpp"

#include "ipc/ipc_types.hpp"
#include "ocvsmd/platform/posix_executor_extension.hpp"
#include "ocvsmd/platform/posix_utils.hpp"
#include "socket_base.hpp"

#include <cetl/cetl.hpp>
#include <cetl/rtti.hpp>
#include <libcyphal/executor.hpp>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace pipe
{

SocketClient::SocketClient(libcyphal::IExecutor& executor, const io::SocketAddress& address)
    : socket_address_{address}
    , posix_executor_ext_{cetl::rtti_cast<platform::IPosixExecutorExtension*>(&executor)}
{
    CETL_DEBUG_ASSERT(posix_executor_ext_ != nullptr, "");

    io_state_.on_rx_msg_payload = [this](const Payload payload) {
        //
        return event_handler_(Event::Message{payload});
    };
}

int SocketClient::start(EventHandler event_handler)
{
    CETL_DEBUG_ASSERT(event_handler, "");
    CETL_DEBUG_ASSERT(io_state_.fd.get() == -1, "");

    event_handler_ = std::move(event_handler);

    if (const auto err = makeSocketHandle())
    {
        logger().error("Failed to make client socket handle: {}.", std::strerror(err));
        return err;
    }

    socket_callback_ = posix_executor_ext_->registerAwaitableCallback(  //
        [this](const auto&) {
            //
            handle_connect();
        },
        platform::IPosixExecutorExtension::Trigger::Writable{io_state_.fd.get()});

    return 0;
}

int SocketClient::makeSocketHandle()
{
    using SocketResult = io::SocketAddress::SocketResult;

    auto maybe_socket = socket_address_.socket(SOCK_STREAM);
    if (const auto* const err = cetl::get_if<SocketResult::Failure>(&maybe_socket))
    {
        logger().error("Failed to create client socket (err={}): {}.", *err, std::strerror(*err));
        return *err;
    }
    auto socket_fd = cetl::get<SocketResult::Success>(std::move(maybe_socket));
    CETL_DEBUG_ASSERT(socket_fd.get() != -1, "");

    if (const int err = socket_address_.connect(socket_fd))
    {
        if (err != EINPROGRESS)
        {
            logger().error("Failed to connect to server (err={}): {}.", err, std::strerror(err));
            return err;
        }
    }

    io_state_.fd = std::move(socket_fd);
    return 0;
}

int SocketClient::send(const Payloads payloads)
{
    return SocketBase::send(io_state_, payloads);
}

int SocketClient::connectSocket(const int fd, const void* const addr_ptr, const std::size_t addr_size) const
{
    if (const auto err = platform::posixSyscallError([fd, addr_ptr, addr_size] {
            //
            return ::connect(fd, static_cast<const sockaddr*>(addr_ptr), addr_size);
        }))
    {
        if (err != EINPROGRESS)
        {
            logger().error("Failed to connect to server: {}.", std::strerror(err));
            return err;
        }
    }
    return 0;
}

void SocketClient::handle_connect()
{
    socket_callback_.reset();

    int so_error = 0;
    if (const auto err = platform::posixSyscallError([this, &so_error] {
            //
            socklen_t len = sizeof(so_error);
            return ::getsockopt(io_state_.fd.get(), SOL_SOCKET, SO_ERROR, &so_error, &len);
        }))
    {
        logger().warn("Failed to query socket error: {}.", std::strerror(err));
        so_error = err;
    }
    if (so_error != 0)
    {
        logger().error("Failed to connect to server: {}.", std::strerror(so_error));
        handle_disconnect();
        return;
    }

    socket_callback_ = posix_executor_ext_->registerAwaitableCallback(  //
        [this](const auto&) {
            //
            handle_receive();
        },
        platform::IPosixExecutorExtension::Trigger::Readable{io_state_.fd.get()});

    event_handler_(Event::Connected{});
}

void SocketClient::handle_receive()
{
    if (const auto err = receiveData(io_state_))
    {
        if (err == -1)
        {
            logger().debug("End of server stream - closing connection.");
        }
        else
        {
            logger().warn("Failed to handle server response - closing connection (err={}): {}.",
                          err,
                          std::strerror(err));
        }

        handle_disconnect();
    }
}

void SocketClient::handle_disconnect()
{
    socket_callback_.reset();

    io_state_.fd.reset();
    io_state_.rx_partial_size = 0;
    io_state_.rx_msg_part.emplace<IoState::MsgHeader>();

    event_handler_(Event::Disconnected{});
}

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd
