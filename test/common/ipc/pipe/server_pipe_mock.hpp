//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_SERVER_PIPE_MOCK_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_SERVER_PIPE_MOCK_HPP_INCLUDED

#include "ipc/pipe/server_pipe.hpp"

#include "io/socket_buffer.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "ref_wrapper.hpp"

#include <gmock/gmock.h>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace pipe
{

class ServerPipeMock : public ServerPipe
{
public:
    struct Wrapper final : RefWrapper<ServerPipe, ServerPipeMock>
    {
        using RefWrapper::RefWrapper;

        // MARK: ServerPipe

        sdk::OptError start(EventHandler event_handler) override
        {
            reference().event_handler_ = event_handler;
            return reference().start(event_handler);
        }

        sdk::OptError send(const ClientId client_id, io::SocketBuffer& sock_buff) override
        {
            return reference().send(client_id, sock_buff);
        }

    };  // Wrapper

    MOCK_METHOD(void, deinit, (), (const));
    MOCK_METHOD(sdk::OptError, start, (EventHandler event_handler), (override));
    MOCK_METHOD(sdk::OptError, send, (const ClientId client_id, io::SocketBuffer& sock_buff), (override));

    // MARK: Data members:

    // NOLINTBEGIN
    EventHandler event_handler_;
    // NOLINTEND

};  // ServerPipeMock

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_SERVER_PIPE_MOCK_HPP_INCLUDED
