//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "socket_client.hpp"

#include "ipc/ipc_types.hpp"
#include "ocvsmd/platform/posix_executor_extension.hpp"
#include "ocvsmd/platform/posix_utils.hpp"
#include "ocvsmd/sdk/defines.hpp"
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

sdk::ErrorCode SocketClient::start(EventHandler event_handler)
{
    CETL_DEBUG_ASSERT(event_handler, "");
    CETL_DEBUG_ASSERT(io_state_.fd.get() == -1, "");

    event_handler_ = std::move(event_handler);

    const auto error_code = makeSocketHandle();
    if (error_code != sdk::ErrorCode::Success)
    {
        logger().error("Failed to make client socket handle (err={}).", error_code);
        return error_code;
    }

    socket_callback_ = posix_executor_ext_->registerAwaitableCallback(  //
        [this](const auto&) {
            //
            handle_connect();
        },
        platform::IPosixExecutorExtension::Trigger::Writable{io_state_.fd.get()});

    return sdk::ErrorCode::Success;
}

sdk::ErrorCode SocketClient::makeSocketHandle()
{
    using SocketResult = io::SocketAddress::SocketResult;

    auto maybe_socket = socket_address_.socket(SOCK_STREAM);
    if (const auto* const failure = cetl::get_if<SocketResult::Failure>(&maybe_socket))
    {
        logger().error("Failed to create client socket (err={}).", *failure);
        return *failure;
    }
    auto socket_fd = cetl::get<SocketResult::Success>(std::move(maybe_socket));
    CETL_DEBUG_ASSERT(socket_fd.get() != -1, "");

    const sdk::ErrorCode error_code = socket_address_.connect(socket_fd);
    if ((error_code != sdk::ErrorCode::Success) && (error_code != sdk::ErrorCode::OperationInProgress))
    {
        logger().error("Failed to connect to server (err={}).", error_code);
        return error_code;
    }

    io_state_.fd = std::move(socket_fd);
    return sdk::ErrorCode::Success;
}

sdk::ErrorCode SocketClient::send(const Payloads payloads)
{
    return SocketBase::send(io_state_, payloads);
}

sdk::ErrorCode SocketClient::connectSocket(const int fd, const void* const addr_ptr, const std::size_t addr_size) const
{
    if (const int err = platform::posixSyscallError([fd, addr_ptr, addr_size] {
            //
            return ::connect(fd, static_cast<const sockaddr*>(addr_ptr), addr_size);
        }))
    {
        if (err != EINPROGRESS)
        {
            logger().error("Failed to connect to server: {}.", std::strerror(err));
            return static_cast<sdk::ErrorCode>(err);
        }
    }
    return sdk::ErrorCode::Success;
}

void SocketClient::handle_connect()
{
    socket_callback_.reset();

    int so_error = 0;
    if (const int err = platform::posixSyscallError([this, &so_error] {
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
    const auto error_code = receiveData(io_state_);
    if (error_code != sdk::ErrorCode::Success)
    {
        if (error_code == sdk::ErrorCode::Disconnected)
        {
            logger().debug("End of server stream - closing connection.");
        }
        else
        {
            logger().warn("Failed to handle server response - closing connection (err={}).", error_code);
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
