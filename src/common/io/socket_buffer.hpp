//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IO_SOCKET_BUFFER_HPP_INCLUDED
#define OCVSMD_COMMON_IO_SOCKET_BUFFER_HPP_INCLUDED

#include <cetl/pf20/cetlpf.hpp>

#include <libcyphal/transport/scattered_buffer.hpp>

#include <cstdint>
#include <list>

namespace ocvsmd
{
namespace common
{
namespace io
{

using Payload = cetl::span<const std::uint8_t>;

class SocketBuffer final
{
public:
    SocketBuffer() = default;

    explicit SocketBuffer(const Payload payload)
    {
        append(payload);
    }

    std::size_t size() const noexcept
    {
        return size_;
    }

    const std::list<Payload>& fragments() const
    {
        return payloads_;
    }

    void prepend(const Payload payload) noexcept
    {
        if (!payload.empty())
        {
            size_ += payload.size();
            payloads_.push_front(payload);
        }
    }

    void append(const Payload payload) noexcept
    {
        if (!payload.empty())
        {
            size_ += payload.size();
            payloads_.push_back(payload);
        }
    }

private:
    std::size_t        size_{0};
    std::list<Payload> payloads_;

};  // SocketBuffer

}  // namespace io
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IO_SOCKET_BUFFER_HPP_INCLUDED
