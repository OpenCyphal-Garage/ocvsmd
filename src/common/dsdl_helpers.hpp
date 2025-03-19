//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_DSDL_HELPERS_HPP_INCLUDED
#define OCVSMD_COMMON_DSDL_HELPERS_HPP_INCLUDED

#include "ocvsmd/sdk/defines.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf20/cetlpf.hpp>

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
CETL_NODISCARD static auto tryDeserializePayload(const cetl::span<const std::uint8_t> payload, Message& out_message)
{
    return deserialize(out_message, {payload.data(), payload.size()});
}

template <typename Message, typename Action>
CETL_NODISCARD static sdk::OptError tryPerformOnSerialized(const Message& message, Action&& action)
{
    // Try to serialize the message to raw payload buffer.
    //
    // Next nolint b/c we use a buffer to serialize the message, so no need to zero it (and performance better).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
    std::array<std::uint8_t, Message::_traits_::SerializationBufferSizeBytes> buffer;
    //
    const auto result_size = serialize(message, {buffer.data(), buffer.size()});
    if (!result_size)
    {
        return sdk::OptError{sdk::Error::Code::InvalidArgument};
    }

    const cetl::span<const std::uint8_t> bytes{buffer.data(), result_size.value()};
    return std::forward<Action>(action)(bytes);
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
    std::array<std::uint8_t, BufferSize> buffer;
    //
    const auto result_size = serialize(message, {buffer.data(), buffer.size()});
    if (!result_size)
    {
        return sdk::OptError{sdk::Error::Code::InvalidArgument};
    }

    const cetl::span<const std::uint8_t> bytes{buffer.data(), result_size.value()};
    return std::forward<Action>(action)(bytes);
}

template <typename Message, std::size_t BufferSize, bool IsOnStack, typename Action>
CETL_NODISCARD static auto tryPerformOnSerialized(  //
    const Message& message,
    Action&&       action) -> std::enable_if_t<!IsOnStack, sdk::OptError>
{
    // Try to serialize the message to raw payload buffer.
    //
    using ArrayOfBytes = std::array<std::uint8_t, BufferSize>;
    const std::unique_ptr<ArrayOfBytes> buffer{new ArrayOfBytes};
    //
    const auto result_size = serialize(message, {buffer->data(), buffer->size()});
    if (!result_size)
    {
        return sdk::OptError{sdk::Error::Code::InvalidArgument};
    }

    const cetl::span<const std::uint8_t> bytes{buffer->data(), result_size.value()};
    return std::forward<Action>(action)(bytes);
}

}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_DSDL_HELPERS_HPP_INCLUDED
