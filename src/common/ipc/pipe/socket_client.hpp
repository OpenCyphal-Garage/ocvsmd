//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_PIPE_SOCKET_CLIENT_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_PIPE_SOCKET_CLIENT_HPP_INCLUDED

#include "client_pipe.hpp"
#include "io/socket_address.hpp"
#include "ipc/ipc_types.hpp"
#include "ocvsmd/platform/posix_executor_extension.hpp"
#include "socket_base.hpp"

#include <cetl/cetl.hpp>
#include <libcyphal/executor.hpp>

#include <cstddef>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace pipe
{

class SocketClient final : public SocketBase, public ClientPipe
{
public:
    SocketClient(libcyphal::IExecutor& executor, const io::SocketAddress& address);

    SocketClient(const SocketClient&)                = delete;
    SocketClient(SocketClient&&) noexcept            = delete;
    SocketClient& operator=(const SocketClient&)     = delete;
    SocketClient& operator=(SocketClient&&) noexcept = delete;

    ~SocketClient() override = default;

private:
    int  makeSocketHandle();
    int  connectSocket(const int fd, const void* const addr_ptr, const std::size_t addr_size) const;
    void handle_connect();
    void handle_receive();
    void handle_disconnect();

    // ClientPipe
    //
    CETL_NODISCARD int start(EventHandler event_handler) override;
    CETL_NODISCARD int send(const Payloads payloads) override;

    io::SocketAddress                        socket_address_;
    platform::IPosixExecutorExtension* const posix_executor_ext_;
    IoState                                  io_state_;
    libcyphal::IExecutor::Callback::Any      socket_callback_;
    EventHandler                             event_handler_;

};  // SocketClient

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_PIPE_SOCKET_CLIENT_HPP_INCLUDED
