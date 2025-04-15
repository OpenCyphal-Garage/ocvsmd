//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_CYPHAL_CAN_TRANSPORT_BAG_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_CYPHAL_CAN_TRANSPORT_BAG_HPP_INCLUDED

#include "any_transport_bag.hpp"
#include "config.hpp"
#include "engine_helpers.hpp"
#include "platform/can/can_media.hpp"
#include "transport_helpers.hpp"

#include <canard.h>

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/transport/can/can_transport.hpp>
#include <libcyphal/transport/can/can_transport_impl.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/types.hpp>

#include <cstddef>
#include <memory>
#include <utility>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace cyphal
{

/// Holds (internally) instance of the CAN transport and its media (if any).
///
class CanTransportBag final : public AnyTransportBag
{
    /// Defines private specification for making interface unique ptr.
    ///
    struct Spec
    {
        explicit Spec() = default;
    };

public:
    Transport& getTransport() const override
    {
        CETL_DEBUG_ASSERT(transport_, "");
        return *transport_;
    }

    static Ptr make(cetl::pmr::memory_resource& memory, libcyphal::IExecutor& executor, const Config::Ptr& config)
    {
        CETL_DEBUG_ASSERT(config, "");

        static const std::string can_prefix{"socketcan:"};

        std::string can_ifaces;
        for (const auto& iface : config->getCyphalTransportInterfaces())
        {
            if (0 == iface.compare(0, can_prefix.size(), can_prefix))
            {
                can_ifaces += iface.substr(can_prefix.size());
                can_ifaces += ",";
            }
        }
        const auto can_mtu = config->getCyphalTransportMtu().value_or(CANARD_MTU_CAN_CLASSIC);
        common::getLogger("io")->trace("Attempting to create CAN transport (ifaces=[{}], mtu={})…",
                                       can_ifaces,
                                       can_mtu);

        auto transport_bag = std::make_unique<CanTransportBag>(Spec{}, memory, executor);

        auto& media_collection = transport_bag->media_collection_;
        media_collection.parse(can_ifaces, can_mtu);
        if (media_collection.count() == 0)
        {
            return nullptr;
        }

        auto maybe_transport = makeTransport({memory}, executor, media_collection.span(), TxQueueCapacity);
        if (const auto* const failure = cetl::get_if<libcyphal::transport::FactoryFailure>(&maybe_transport))
        {
            const auto opt_error = cyFailureToOptError(*failure);
            common::getLogger("io")->warn("Failed to create CAN transport (err={}).", opt_error);
            return nullptr;
        }
        transport_bag->transport_ = cetl::get<TransportPtr>(std::move(maybe_transport));

        // To support redundancy (multiple homogeneous interfaces), it's important to have a non-default
        // handler which "swallows" expected transient failures (by returning `nullopt` result).
        // Otherwise, the default Cyphal behavior will fail/interrupt current and future transfers
        // if some of its media encounter transient failures - thus breaking the whole redundancy goal,
        // namely, maintain communication if at least one of the interfaces is still up and running.
        //
        transport_bag->transport_->setTransientErrorHandler([](auto&) { return cetl::nullopt; });
        // transport_bag->transport_->setTransientErrorHandler(TransportHelpers::CanTransientErrorReporter{});

        common::getLogger("io")->debug("Created CAN transport (ifaces={}).", media_collection.count());
        return transport_bag;
    }

    CanTransportBag(Spec, cetl::pmr::memory_resource& memory, libcyphal::IExecutor& executor)
        : memory_{memory}
        , executor_{executor}
        , media_collection_{memory, executor, memory}
    {
    }

private:
    using TransportPtr = libcyphal::UniquePtr<libcyphal::transport::can::ICanTransport>;

    // Our current max `SerializationBufferSizeBytes` is 313 bytes (for `uavcan.node.GetInfo.Response.1.0`)
    // Assuming CAN classic presentation MTU of 7 bytes (plus a bit of overhead like CRC and stuff),
    // let's calculate the required TX queue capacity, and make it twice to accommodate 2 such messages.
    static constexpr std::size_t TxQueueCapacity = 2 * (313U + 8U) / 7U;

    cetl::pmr::memory_resource&       memory_;
    libcyphal::IExecutor&             executor_;
    platform::can::CanMediaCollection media_collection_;
    TransportPtr                      transport_;

};  // CanTransportBag

}  // namespace cyphal
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_CYPHAL_CAN_TRANSPORT_BAG_HPP_INCLUDED
