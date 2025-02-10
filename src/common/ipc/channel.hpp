//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_CHANNEL_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_CHANNEL_HPP_INCLUDED

#include "dsdl_helpers.hpp"
#include "gateway.hpp"
#include "ipc_types.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/common/crc.hpp>

#include <spdlog/fmt/fmt.h>

#include <cerrno>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

class AnyChannel
{
public:
    struct Connected final
    {};

    struct Completed final
    {
        /// Channel completion error code. Zero means success.
        ErrorCode error_code;
    };

    /// Builds a service ID from either the service name (if not empty), or message type name.
    ///
    template <typename Message>
    CETL_NODISCARD static detail::ServiceDesc getServiceDesc(cetl::string_view service_name) noexcept
    {
        const cetl::string_view srv_or_msg_name = !service_name.empty()  //
                                                      ? service_name
                                                      : Message::_traits_::FullNameAndVersion();

        const libcyphal::common::CRC64WE crc64{srv_or_msg_name.cbegin(), srv_or_msg_name.cend()};
        return {crc64.get(), srv_or_msg_name};
    }

protected:
    AnyChannel() = default;

};  // AnyChannel

template <typename Input_, typename Output_>
class Channel final : public AnyChannel
{
public:
    using Input  = Input_;
    using Output = Output_;

    using EventVar     = cetl::variant<Connected, Input, Completed>;
    using EventHandler = std::function<void(const EventVar&)>;

    ~Channel()                                   = default;
    Channel(Channel&& other) noexcept            = default;
    Channel& operator=(Channel&& other) noexcept = default;

    Channel(const Channel&)            = delete;
    Channel& operator=(const Channel&) = delete;

    CETL_NODISCARD int send(const Output& output)
    {
        constexpr std::size_t BufferSize = Output::_traits_::SerializationBufferSizeBytes;
        constexpr bool        IsOnStack  = BufferSize <= MsgSmallPayloadSize;

        return tryPerformOnSerialized<Output, BufferSize, IsOnStack>(  //
            output,
            [this](const auto payload) {
                //
                return gateway_->send(service_id_, payload);
            });
    }

    void complete(const int error_code)
    {
        return gateway_->complete(error_code);
    }

    void subscribe(EventHandler event_handler)
    {
        if (event_handler)
        {
            auto adapter = Adapter{memory_, std::move(event_handler)};
            gateway_->subscribe([adapter = std::move(adapter)](const GatewayEvent::Var& ge_var) {
                //
                return cetl::visit(adapter, ge_var);
            });
        }
        else
        {
            gateway_->subscribe(nullptr);
        }
    }

private:
    friend class ClientRouter;
    friend class ServerRouter;

    using GatewayEvent = detail::Gateway::Event;

    struct Adapter final
    {
        cetl::pmr::memory_resource& memory;            // NOLINT
        EventHandler                ch_event_handler;  // NOLINT

        CETL_NODISCARD int operator()(const GatewayEvent::Connected&) const
        {
            ch_event_handler(Connected{});
            return 0;
        }

        CETL_NODISCARD int operator()(const GatewayEvent::Message& gateway_msg) const
        {
            Input input{&memory};
            if (!tryDeserializePayload(gateway_msg.payload, input))
            {
                // Invalid message payload.
                return EINVAL;
            }

            ch_event_handler(input);
            return 0;
        }

        CETL_NODISCARD int operator()(const GatewayEvent::Completed& completed) const
        {
            ch_event_handler(Completed{completed.error_code});
            return 0;
        }

    };  // Adapter

    Channel(cetl::pmr::memory_resource& memory, detail::Gateway::Ptr gateway, const detail::ServiceDesc::Id service_id)
        : memory_{memory}
        , gateway_{std::move(gateway)}
        , service_id_{service_id}
    {
        CETL_DEBUG_ASSERT(gateway_, "");
    }

    static constexpr std::size_t MsgSmallPayloadSize = 256;

    std::reference_wrapper<cetl::pmr::memory_resource> memory_;
    detail::Gateway::Ptr                               gateway_;
    detail::ServiceDesc::Id                            service_id_;

};  // Channel

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

// MARK: - Formatting

// NOLINTBEGIN
template <>
struct fmt::formatter<ocvsmd::common::ipc::AnyChannel::Connected> : formatter<string_view>
{
    auto format(ocvsmd::common::ipc::AnyChannel::Connected, format_context& ctx) const
    {
        return formatter<string_view>::format("Connected", ctx);
    }
};

template <>
struct fmt::formatter<ocvsmd::common::ipc::AnyChannel::Completed> : formatter<std::string>
{
    auto format(ocvsmd::common::ipc::AnyChannel::Completed completed, format_context& ctx) const
    {
        return format_to(ctx.out(), "Completed(err={})", static_cast<int>(completed.error_code));
    }
};
// NOLINTEND

#endif  // OCVSMD_COMMON_IPC_CHANNEL_HPP_INCLUDED
