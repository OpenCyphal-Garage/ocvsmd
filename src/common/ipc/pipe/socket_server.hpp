//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_PIPE_SOCKET_SERVER_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_PIPE_SOCKET_SERVER_HPP_INCLUDED

#include "client_context.hpp"
#include "io/io.hpp"
#include "io/socket_address.hpp"
#include "ipc/ipc_types.hpp"
#include "ocvsmd/platform/posix_executor_extension.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "server_pipe.hpp"
#include "socket_base.hpp"

#include <cetl/cetl.hpp>
#include <libcyphal/executor.hpp>

#include <cstddef>
#include <unordered_map>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace pipe
{

class SocketServer final : public SocketBase, public ServerPipe
{
public:
    SocketServer(libcyphal::IExecutor& executor, const io::SocketAddress& address);

    SocketServer(const SocketServer&)                = delete;
    SocketServer(SocketServer&&) noexcept            = delete;
    SocketServer& operator=(const SocketServer&)     = delete;
    SocketServer& operator=(SocketServer&&) noexcept = delete;

    ~SocketServer() override = default;

private:
    sdk::OptError  makeSocketHandle();
    void           handleAccept();
    void           handleClientRequest(const ClientId client_id);
    ClientContext* tryFindClientContext(const ClientId client_id);

    // ServerPipe
    //
    CETL_NODISCARD sdk::OptError start(EventHandler event_handler) override;
    CETL_NODISCARD sdk::OptError send(const ClientId client_id, const Payloads payloads) override;

    io::OwnFd                                        server_fd_;
    io::SocketAddress                                socket_address_;
    platform::IPosixExecutorExtension* const         posix_executor_ext_;
    ClientId                                         unique_client_id_counter_;
    EventHandler                                     event_handler_;
    libcyphal::IExecutor::Callback::Any              accept_callback_;
    std::unordered_map<ClientId, ClientContext::Ptr> client_id_to_context_;

};  // SocketServer

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_PIPE_SOCKET_SERVER_HPP_INCLUDED
