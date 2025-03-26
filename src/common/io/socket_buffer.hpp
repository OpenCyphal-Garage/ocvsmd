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

using Payload = libcyphal::transport::PayloadFragment;

class SocketBuffer final : libcyphal::transport::ScatteredBuffer::IFragmentsVisitor
{
public:
    SocketBuffer() = default;

    explicit SocketBuffer(const Payload payload)
    {
        append(payload);
    }

    explicit SocketBuffer(const libcyphal::transport::ScatteredBuffer& scattered_buffer)
    {
        scattered_buffer.forEachFragment(*this);
    }

    std::size_t size() const noexcept
    {
        return size_;
    }

    const std::list<Payload>& listFragments() const
    {
        return payloads_;
    }

    void prepend(const Payload payload) noexcept
    {
        if (isValuePayload(payload))
        {
            size_ += payload.size();
            payloads_.push_front(payload);
        }
    }

    void append(const Payload payload) noexcept
    {
        if (isValuePayload(payload))
        {
            size_ += payload.size();
            payloads_.push_back(payload);
        }
    }

private:
    static bool isValuePayload(const Payload payload)
    {
        return (payload.data() != nullptr) && !payload.empty();
    }

    // IFragmentsVisitor

    void onNext(const Payload payload) override
    {
        if (isValuePayload(payload))
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
