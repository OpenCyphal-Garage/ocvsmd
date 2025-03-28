//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_DSDL_HELPERS_HPP_INCLUDED
#define OCVSMD_COMMON_DSDL_HELPERS_HPP_INCLUDED

#include "io/socket_buffer.hpp"
#include "ocvsmd/sdk/defines.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace ocvsmd
{
namespace common
{

template <typename Message>
CETL_NODISCARD static auto tryDeserializePayload(const io::Payload payload, Message& out_message)
{
    // No lint b/c of integration with Nunavut.
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    return deserialize(out_message, {reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()});
}

template <typename Message, typename Action>
CETL_NODISCARD static sdk::OptError tryPerformOnSerialized(const Message& message, Action&& action)
{
    // Try to serialize the message to raw payload buffer.
    //
    // Next nolint b/c we use a buffer to serialize the message, so no need to zero it (and performance better).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
    std::array<cetl::byte, Message::_traits_::SerializationBufferSizeBytes> buffer;
    //
    // No lint b/c of integration with Nunavut.
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    const auto result_size = serialize(message, {reinterpret_cast<std::uint8_t*>(buffer.data()), buffer.size()});
    if (!result_size)
    {
        return sdk::OptError{sdk::Error::Code::InvalidArgument};
    }

    const io::Payload payload{buffer.data(), result_size.value()};
    return std::forward<Action>(action)(payload);
}

template <typename Message, std::size_t BufferSize, bool IsOnStack, typename Action>
CETL_NODISCARD static auto tryPerformOnSerialized(  //
    const Message& message,
    Action&&       action) -> std::enable_if_t<IsOnStack, sdk::OptError>
{
    // Try to serialize the message to raw payload buffer.
    //
    // Next nolint b/c we use a buffer to serialize the message, so no need to zero it (and performance better).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
    std::array<cetl::byte, BufferSize> buffer;
    //
    // No lint b/c of integration with Nunavut.
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    const auto result_size = serialize(message, {reinterpret_cast<std::uint8_t*>(buffer.data()), buffer.size()});
    if (!result_size)
    {
        return sdk::OptError{sdk::Error::Code::InvalidArgument};
    }

    const io::Payload payload{buffer.data(), result_size.value()};
    return std::forward<Action>(action)(payload);
}

template <typename Message, std::size_t BufferSize, bool IsOnStack, typename Action>
CETL_NODISCARD static auto tryPerformOnSerialized(  //
    const Message& message,
    Action&&       action) -> std::enable_if_t<!IsOnStack, sdk::OptError>
{
    // Try to serialize the message to raw payload buffer.
    //
    using ArrayOfBytes = std::array<cetl::byte, BufferSize>;
    const std::unique_ptr<ArrayOfBytes> buffer{new ArrayOfBytes};
    //
    // No lint b/c of integration with Nunavut.
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    const auto result_size = serialize(message, {reinterpret_cast<std::uint8_t*>(buffer->data()), buffer->size()});
    if (!result_size)
    {
        return sdk::OptError{sdk::Error::Code::InvalidArgument};
    }

    const io::Payload payload{buffer->data(), result_size.value()};
    return std::forward<Action>(action)(payload);
}

}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_DSDL_HELPERS_HPP_INCLUDED
