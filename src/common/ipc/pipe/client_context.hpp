//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_PIPE_CLIENT_CONTEXT_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_PIPE_CLIENT_CONTEXT_HPP_INCLUDED

#include "io/io.hpp"
#include "logging.hpp"
#include "ocvsmd/platform/posix_utils.hpp"
#include "server_pipe.hpp"
#include "socket_base.hpp"

#include <cetl/cetl.hpp>
#include <libcyphal/executor.hpp>

#include <memory>
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

class ClientContext final
{
public:
    using Ptr = std::unique_ptr<ClientContext>;

    ClientContext(const ServerPipe::ClientId id, io::OwnFd&& fd, Logger& logger)
        : id_{id}
        , logger_{logger}
    {
        CETL_DEBUG_ASSERT(fd.get() != -1, "");

        logger_.trace("ClientContext(fd={}, id={}).", fd.get(), id_);
        io_state_.fd = std::move(fd);
    }

    ~ClientContext()
    {
        logger_.trace("~ClientContext(fd={}, id={}).", io_state_.fd.get(), id_);
    }

    ClientContext(const ClientContext&)                = delete;
    ClientContext(ClientContext&&) noexcept            = delete;
    ClientContext& operator=(const ClientContext&)     = delete;
    ClientContext& operator=(ClientContext&&) noexcept = delete;

    SocketBase::IoState& state() noexcept
    {
        return io_state_;
    }

    void setCallback(libcyphal::IExecutor::Callback::Any&& fd_callback)
    {
        fd_callback_ = std::move(fd_callback);
    }

private:
    const ServerPipe::ClientId          id_;
    Logger&                             logger_;
    SocketBase::IoState                 io_state_;
    libcyphal::IExecutor::Callback::Any fd_callback_;

};  // ClientContext

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_PIPE_CLIENT_CONTEXT_HPP_INCLUDED
