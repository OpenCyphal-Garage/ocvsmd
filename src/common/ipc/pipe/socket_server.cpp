//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "socket_server.hpp"

#include "client_context.hpp"
#include "common_helpers.hpp"
#include "io/io.hpp"
#include "io/socket_address.hpp"
#include "io/socket_buffer.hpp"
#include "logging.hpp"
#include "ocvsmd/platform/posix_executor_extension.hpp"
#include "ocvsmd/platform/posix_utils.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "socket_base.hpp"

#include <cetl/cetl.hpp>
#include <cetl/rtti.hpp>
#include <libcyphal/executor.hpp>

#include <cstring>
#include <memory>
#include <sys/socket.h>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace pipe
{
namespace
{

constexpr int MaxConnections = 32;

}  // namespace

SocketServer::SocketServer(libcyphal::IExecutor& executor, const io::SocketAddress& address)
    : socket_address_{address}
    , posix_executor_ext_{cetl::rtti_cast<platform::IPosixExecutorExtension*>(&executor)}
    , unique_client_id_counter_{0}
{
    CETL_DEBUG_ASSERT(posix_executor_ext_ != nullptr, "");
}

sdk::OptError SocketServer::start(EventHandler event_handler)
{
    CETL_DEBUG_ASSERT(event_handler, "");
    CETL_DEBUG_ASSERT(server_fd_.get() == -1, "");

    event_handler_ = std::move(event_handler);

    if (const auto opt_error = makeSocketHandle())
    {
        logger().error("Failed to make server socket handle (err={}).", *opt_error);
        return opt_error;
    }

    if (const int err = platform::posixSyscallError([this] {
            //
            return ::listen(server_fd_.get(), MaxConnections);
        }))
    {
        logger().error("Failed to listen on server socket: {}.", std::strerror(err));
        return errnoToError(err);
    }

    accept_callback_ = posix_executor_ext_->registerAwaitableCallback(  //
        [this](const auto&) {
            //
            handleAccept();
        },
        platform::IPosixExecutorExtension::Trigger::Readable{server_fd_.get()});

    return sdk::OptError{};
}

sdk::OptError SocketServer::makeSocketHandle()
{
    using SocketResult = io::SocketAddress::SocketResult;

    auto maybe_socket = socket_address_.socket(SOCK_STREAM);
    if (auto* const failure = cetl::get_if<SocketResult::Failure>(&maybe_socket))
    {
        logger().error("Failed to create server socket (err={}).", *failure);
        return sdk::OptError{*failure};
    }
    auto socket_fd = cetl::get<SocketResult::Success>(std::move(maybe_socket));
    CETL_DEBUG_ASSERT(socket_fd.get() != -1, "");

    // Set SO_REUSEADDR to allow binding to the same address.
    // Otherwise, you have to wait for 5 minutes after the server is stopped to bind to the same address.
    if (const int err = platform::posixSyscallError([this, &socket_fd] {
            //
            constexpr int enable = 1;
            return ::setsockopt(socket_fd.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
        }))
    {
        logger().error("Failed to set server socket SO_REUSEADDR=1: {}.", std::strerror(err));
        return errnoToError(err);
    }

    if (const auto opt_error = socket_address_.bind(socket_fd))
    {
        logger().error("Failed to bind server socket (err={}).", *opt_error);
        return opt_error;
    }

    server_fd_ = std::move(socket_fd);
    return sdk::OptError{};
}

sdk::OptError SocketServer::send(const ClientId client_id, io::SocketBuffer& sock_buff)
{
    if (auto* const client_context = tryFindClientContext(client_id))
    {
        return SocketBase::send(client_context->state(), sock_buff);
    }

    logger().warn("Client context is not found (id={}).", client_id);
    return sdk::Error{sdk::Error::Code::InvalidArgument};
}

void SocketServer::handleAccept()
{
    CETL_DEBUG_ASSERT(server_fd_.get() != -1, "");

    io::SocketAddress client_address;
    if (auto client_fd = client_address.accept(server_fd_))
    {
        const ClientId new_client_id = ++unique_client_id_counter_;

        // Log to default logger (syslog) the client connection.
        getLogger("")->debug("New client connection (id={}, addr='{}').", new_client_id, client_address.toString());

        const int raw_fd = client_fd->get();
        CETL_DEBUG_ASSERT(raw_fd != -1, "");

        auto client_context = std::make_unique<ClientContext>(new_client_id, std::move(*client_fd), logger());
        //
        client_context->setCallback(posix_executor_ext_->registerAwaitableCallback(
            [this, new_client_id](const auto&) {
                //
                handleClientRequest(new_client_id);
            },
            platform::IPosixExecutorExtension::Trigger::Readable{raw_fd}));
        //
        client_context->state().on_rx_msg_payload = [this, new_client_id](const io::Payload payload) {
            //
            return event_handler_(Event::Message{new_client_id, payload});
        };

        client_id_to_context_.emplace(new_client_id, std::move(client_context));

        event_handler_(Event::Connected{new_client_id});
    }
}

void SocketServer::handleClientRequest(const ClientId client_id)
{
    auto* const client_context = tryFindClientContext(client_id);
    CETL_DEBUG_ASSERT(client_context, "");
    auto& state = client_context->state();

    if (const auto opt_error = receiveData(state))
    {
        if (sdk::Error::Code::Disconnected == opt_error->getCode())
        {
            logger().debug("End of client stream - closing connection (id={}, fd={}).", client_id, state.fd.get());
        }
        else
        {
            logger().warn("Failed to handle client request - closing connection (id={}, fd={}, err={}).",
                          client_id,
                          state.fd.get(),
                          *opt_error);
        }

        client_id_to_context_.erase(client_id);
        event_handler_(Event::Disconnected{client_id});
    }
}

ClientContext* SocketServer::tryFindClientContext(const ClientId client_id)
{
    const auto id_and_context = client_id_to_context_.find(client_id);
    if (id_and_context != client_id_to_context_.end())
    {
        return id_and_context->second.get();
    }
    return nullptr;
}

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd
