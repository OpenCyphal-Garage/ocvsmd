//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "socket_base.hpp"

#include "common_helpers.hpp"
#include "io/socket_buffer.hpp"
#include "ocvsmd/platform/posix_utils.hpp"
#include "ocvsmd/sdk/defines.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <sys/socket.h>
#include <sys/types.h>
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

/// Returns `true` if the given error code indicates that the operation is not ready yet.
///
/// `EAGAIN` and `EWOULDBLOCK` are considered as not ready conditions.
///
bool isNotReadyCondition(const int err)
{
#if EAGAIN == EWOULDBLOCK
    return err == EAGAIN;
#else
    return (err == EAGAIN) || (err == EWOULDBLOCK);
#endif
}

constexpr std::uint32_t MsgHeaderSignature = 0x5356434F;     // 'OCVS'
constexpr std::size_t   MsgPayloadMaxSize  = 1ULL << 20ULL;  // 1 MB

}  // namespace

sdk::OptError SocketBase::send(const IoState& io_state, io::SocketBuffer& sock_buff) const
{
    // 1. Prepend the message header (signature and total size of the following fragments).
    //
    CETL_DEBUG_ASSERT(sock_buff.size() <= std::numeric_limits<std::uint32_t>::max(), "");
    const IoState::MsgHeader msg_header{MsgHeaderSignature, static_cast<std::uint32_t>(sock_buff.size())};
    // NOLINTNEXTLINE(*-reinterpret-cast)
    sock_buff.prepend({reinterpret_cast<const cetl::byte*>(&msg_header), sizeof(msg_header)});

    // 2. Write all payload fragments.
    //
    for (const auto payload : sock_buff.listFragments())
    {
        if (const int err = platform::posixSyscallError([payload, &io_state] {
                //
                return ::send(io_state.fd.get(), payload.data(), payload.size(), MSG_DONTWAIT);
            }))
        {
            logger_->error("SocketBase: Failed to send msg payload (fd={}): {}.",
                           io_state.fd.get(),
                           std::strerror(err));
            return errnoToError(err);
        }
    }
    return sdk::OptError{};
}

sdk::OptError SocketBase::receiveData(IoState& io_state) const
{
    // 1. Receive and validate the message header.
    //
    if (auto* const msg_header_ptr = cetl::get_if<IoState::MsgHeader>(&io_state.rx_msg_part))
    {
        auto& msg_header = *msg_header_ptr;

        CETL_DEBUG_ASSERT(io_state.rx_partial_size < sizeof(msg_header), "");
        if (io_state.rx_partial_size < sizeof(msg_header))
        {
            // Try to read the remaining part of the message header.
            //
            ssize_t bytes_read = 0;
            if (const int err = platform::posixSyscallError([&io_state, &bytes_read, &msg_header] {
                    //
                    // No lint b/c of low-level (potentially partial) reading.
                    // NOLINTNEXTLINE(*-reinterpret-cast, *-pointer-arithmetic)
                    auto* const dst_buf = reinterpret_cast<std::uint8_t*>(&msg_header) + io_state.rx_partial_size;
                    //
                    const auto bytes_to_read = sizeof(msg_header) - io_state.rx_partial_size;
                    return bytes_read        = ::recv(io_state.fd.get(), dst_buf, bytes_to_read, MSG_DONTWAIT);
                }))
            {
                if (isNotReadyCondition(err))
                {
                    // No data available yet - that's ok, the next attempt will try to read again.
                    //
                    logger_->trace("Msg header read is not ready (fd={}).", io_state.fd.get());
                    return sdk::OptError{};
                }

                if (err == ECONNRESET)
                {
                    logger_->debug("Connection reset by peer (fd={}).", io_state.fd.get());
                    return sdk::Error{sdk::Error::Code::Disconnected, err};  // EOF
                }

                logger_->error("Failed to read msg header (fd={}, err={}): {}.",
                               io_state.fd.get(),
                               err,
                               std::strerror(err));
                return errnoToError(err);
            }

            // Progress the partial read state.
            //
            io_state.rx_partial_size += bytes_read;
            CETL_DEBUG_ASSERT(io_state.rx_partial_size <= sizeof(msg_header), "");
            if (bytes_read == 0)
            {
                logger_->debug("Zero bytes of msg header read - end of stream (fd={}).", io_state.fd.get());
                return sdk::Error{sdk::Error::Code::Disconnected};  // EOF
            }
            if (io_state.rx_partial_size < sizeof(msg_header))
            {
                // Not enough data yet - that's ok, the next attempt will try to read the rest.
                return sdk::OptError{};
            }

            // Validate the message header.
            // Just in case validate also the payload size to be within the reasonable limits.
            // Zero payload size is also considered invalid (b/c we always expect non-empty `Route` payload).
            //
            if ((msg_header.signature != MsgHeaderSignature)  //
                || (msg_header.payload_size == 0) || (msg_header.payload_size > MsgPayloadMaxSize))
            {
                logger_->error("Invalid msg header read - closing invalid stream (fd={}, payload_size={}).",
                               io_state.fd.get(),
                               msg_header.payload_size);
                return sdk::Error{sdk::Error::Code::InvalidArgument};
            }
        }

        // The message header has been read and validated.
        // Switch to the next part - message payload.
        //
        io_state.rx_partial_size = 0;
        auto payload_buffer      = std::make_unique<cetl::byte[]>(msg_header.payload_size);  // NOLINT(*-avoid-c-arrays)
        io_state.rx_msg_part.emplace<IoState::MsgPayload>(
            IoState::MsgPayload{msg_header.payload_size, std::move(payload_buffer)});
    }

    // 2. Read message payload.
    //
    if (auto* const msg_payload_ptr = cetl::get_if<IoState::MsgPayload>(&io_state.rx_msg_part))
    {
        auto& msg_payload = *msg_payload_ptr;

        CETL_DEBUG_ASSERT(io_state.rx_partial_size < msg_payload.size, "");
        if (io_state.rx_partial_size < msg_payload.size)
        {
            ssize_t bytes_read = 0;
            if (const int err = platform::posixSyscallError([&io_state, &bytes_read, &msg_payload] {
                    //
                    auto* const dst_buf = msg_payload.buffer.get() + io_state.rx_partial_size;
                    //
                    const auto bytes_to_read = msg_payload.size - io_state.rx_partial_size;
                    return bytes_read        = ::recv(io_state.fd.get(), dst_buf, bytes_to_read, MSG_DONTWAIT);
                }))
            {
                if (isNotReadyCondition(err))
                {
                    // No data available yet - that's ok, the next attempt will try to read again.
                    //
                    logger_->trace("Msg payload read is not ready (fd={}).", io_state.fd.get());
                    return sdk::OptError{};
                }

                logger_->error("Failed to read msg payload (fd={}): {}.", io_state.fd.get(), std::strerror(err));
                return errnoToError(err);
            }

            // Progress the partial read state.
            //
            io_state.rx_partial_size += bytes_read;
            CETL_DEBUG_ASSERT(io_state.rx_partial_size <= msg_payload.size, "");
            if (bytes_read == 0)
            {
                logger_->debug("Zero bytes of msg payload read - end of stream (fd={}).", io_state.fd.get());
                return sdk::Error{sdk::Error::Code::Disconnected};  // EOF
            }
            if (io_state.rx_partial_size < msg_payload.size)
            {
                // Not enough data yet - that's ok, the next attempt will try to read the rest.
                return sdk::OptError{};
            }
        }

        // Message payload has been completely received.
        // Switch to the first part - the message header again.
        //
        io_state.rx_partial_size = 0;
        const auto payload       = std::move(msg_payload);
        io_state.rx_msg_part.emplace<IoState::MsgHeader>();

        io_state.on_rx_msg_payload(io::Payload{payload.buffer.get(), payload.size});
    }

    return sdk::OptError{};
}

}  // namespace pipe
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd
