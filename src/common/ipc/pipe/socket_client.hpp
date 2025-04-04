//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_PIPE_SOCKET_CLIENT_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_PIPE_SOCKET_CLIENT_HPP_INCLUDED

#include "client_pipe.hpp"
#include "io/socket_address.hpp"
#include "io/socket_buffer.hpp"
#include "ocvsmd/platform/posix_executor_extension.hpp"
#include "ocvsmd/sdk/defines.hpp"
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
    sdk::OptError makeSocketHandle();
    sdk::OptError connectSocket(const int fd, const void* const addr_ptr, const std::size_t addr_size) const;
    void          handleConnect();
    void          handleReceive();
    void          handleDisconnect();

    // ClientPipe
    //
    CETL_NODISCARD sdk::OptError start(EventHandler event_handler) override;
    CETL_NODISCARD sdk::OptError send(io::SocketBuffer& sock_buff) override;

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
