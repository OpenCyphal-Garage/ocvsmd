//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IO_SOCKET_BUFFER_HPP_INCLUDED
#define OCVSMD_COMMON_IO_SOCKET_BUFFER_HPP_INCLUDED

#include <libcyphal/transport/scattered_buffer.hpp>

#include <cstddef>
#include <list>

namespace ocvsmd
{
namespace common
{
namespace io
{

/// Defines immutable fragment of raw data (as span of const bytes).
///
/// It's made exactly the same as the one from libcyphal to avoid unnecessary conversions.
///
using Payload = libcyphal::transport::PayloadFragment;

/// `SocketBuffer` is a helper class that stores a list of payload fragments.
///
/// Note that the class doesn't own the data, it just stores references to it.
///
/// In use to collect references to multiple fragments of data to be sent over an IPC socket.
/// Such fragments naturally materialize as the IPC stack serializes and then wraps a user message/data with
/// additional layers of internal housekeeping information (like header, IPC commands, footers, etc.).
///
/// Also, the collected list of fragments could be used for vectorized I/O operations (see `::sendmsg`).
///
class SocketBuffer final : libcyphal::transport::ScatteredBuffer::IFragmentsVisitor
{
public:
    SocketBuffer() = default;

    /// Constructs a `SocketBuffer` from a single payload fragment.
    ///
    /// Constructed buffer should not outlive the payload.
    ///
    /// @param payload The initial payload. Ignored if empty (or `nullptr`).
    ///
    explicit SocketBuffer(const Payload payload)
    {
        append(payload);
    }

    /// Constructs a `SocketBuffer` from fragments of Cyphal transport `ScatteredBuffer` instance.
    ///
    /// Constructed buffer should not outlive the `scattered_buffer` instance.
    ///
    /// @param scattered_buffer The initial data. Empty (or `nullptr`) fragments are ignored.
    ///
    explicit SocketBuffer(const libcyphal::transport::ScatteredBuffer& scattered_buffer)
    {
        scattered_buffer.forEachFragment(*this);
    }

    /// Gets the total size of all fragments.
    ///
    std::size_t size() const noexcept
    {
        return size_;
    }

    /// Gets the list of fragments currently stored inside.
    ///
    /// Useful for final data sending (either "one by one" or by building a single vectorized I/O).
    ///
    const std::list<Payload>& listFragments() const
    {
        return payloads_;
    }

    /// Prepends a currently stored list of fragments with a new payload.
    ///
    /// Useful for adding a header to the head of the previous data.
    ///
    /// @param payload The payload to prepend to head. Ignored if empty (or `nullptr`).
    ///
    void prepend(const Payload payload) noexcept
    {
        if (isValidPayload(payload))
        {
            size_ += payload.size();
            payloads_.push_front(payload);
        }
    }

    /// Appends a currently stored list of fragments with a new payload.
    ///
    /// Useful for adding an extra data (or a header) to the tail of the previous data.
    ///
    /// @param payload The payload to append to tail. Ignored if empty (or `nullptr`).
    ///
    void append(const Payload payload) noexcept
    {
        if (isValidPayload(payload))
        {
            size_ += payload.size();
            payloads_.push_back(payload);
        }
    }

private:
    static bool isValidPayload(const Payload payload)
    {
        return (payload.data() != nullptr) && !payload.empty();
    }

    // IFragmentsVisitor

    void onNext(const Payload payload) override
    {
        if (isValidPayload(payload))
        {
            size_ += payload.size();
            payloads_.push_back(payload);
        }
    }

    std::size_t        size_{0};
    std::list<Payload> payloads_;

};  // SocketBuffer

}  // namespace io
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IO_SOCKET_BUFFER_HPP_INCLUDED
