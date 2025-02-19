//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_PIPE_SOCKET_BASE_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_PIPE_SOCKET_BASE_HPP_INCLUDED

#include "io/io.hpp"
#include "ipc/ipc_types.hpp"
#include "logging.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace pipe
{

class SocketBase
{
public:
    struct IoState final
    {
        struct MsgHeader final
        {
            std::uint32_t signature{0};
            std::uint32_t payload_size{0};
        };
        struct MsgPayload final
        {
            std::uint32_t                   size{0};
            std::unique_ptr<std::uint8_t[]> buffer{nullptr};  // NOLINT(*-avoid-c-arrays)
        };
        using MsgPart = cetl::variant<MsgHeader, MsgPayload>;

        io::OwnFd                   fd;
        std::size_t                 rx_partial_size{0};
        MsgPart                     rx_msg_part{MsgHeader{}};
        std::function<int(Payload)> on_rx_msg_payload;

    };  // IoState

    SocketBase(const SocketBase&)                = delete;
    SocketBase(SocketBase&&) noexcept            = delete;
    SocketBase& operator=(const SocketBase&)     = delete;
    SocketBase& operator=(SocketBase&&) noexcept = delete;

protected:
    SocketBase()  = default;
    ~SocketBase() = default;

    Logger& logger() const noexcept
    {
        return *logger_;
    }

    CETL_NODISCARD int send(const IoState& io_state, const Payloads payloads) const;
    CETL_NODISCARD int receiveData(IoState& io_state) const;

private:
    LoggerPtr logger_{getLogger("ipc")};

};  // SocketBase

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_PIPE_SOCKET_BASE_HPP_INCLUDED
