//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "server_router.hpp"

#include "common_helpers.hpp"
#include "dsdl_helpers.hpp"
#include "gateway.hpp"
#include "ipc_types.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "pipe/server_pipe.hpp"

#include "ocvsmd/common/ipc/RouteChannelMsg_0_1.hpp"
#include "ocvsmd/common/ipc/RouteConnect_0_1.hpp"
#include "ocvsmd/common/ipc/Route_0_2.hpp"
#include "uavcan/primitive/Empty_1_0.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/visit_helpers.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace
{

/// Defines implementation of the IPC server-side router.
///
/// It subscribes to the server pipe events and dispatches them to the registered channel factories.
/// In case of the pipe disconnection, it is broadcast-ed to all currently existing gateways (aka channels).
///
class ServerRouterImpl final : public ServerRouter
{
public:
    ServerRouterImpl(cetl::pmr::memory_resource& memory, pipe::ServerPipe::Ptr server_pipe)
        : memory_{memory}
        , server_pipe_{std::move(server_pipe)}
        , logger_{getLogger("ipc")}
    {
        CETL_DEBUG_ASSERT(server_pipe_, "");
    }

    // ServerRouter

    CETL_NODISCARD cetl::pmr::memory_resource& memory() override
    {
        return memory_;
    }

    CETL_NODISCARD sdk::OptError start() override
    {
        return server_pipe_->start([this](const auto& pipe_event_var) {
            //
            return cetl::visit(
                [this](const auto& pipe_event) {
                    //
                    return handlePipeEvent(pipe_event);
                },
                pipe_event_var);
        });
    }

    void registerChannelFactory(const detail::ServiceDesc service_desc,  //
                                TypeErasedChannelFactory  channel_factory) override
    {
        logger_->trace("Registering '{}' service (id=0x{:X}).", service_desc.name, service_desc.id);
        service_id_to_channel_factory_[service_desc.id] = std::move(channel_factory);
    }

private:
    struct Endpoint final
    {
        using Tag      = std::uint64_t;
        using ClientId = pipe::ServerPipe::ClientId;

        const Tag      tag;
        const ClientId client_id;
    };

    /// Defines private IPC gateway entity of this server-side IPC router.
    ///
    /// Gateway is a glue between this IPC router and a service channel.
    /// Lifetime of a gateway is exactly the same as the lifetime of the associated service channel.
    ///
    class GatewayImpl final : public std::enable_shared_from_this<GatewayImpl>, public detail::Gateway
    {
        struct Private
        {
            explicit Private() = default;
        };

    public:
        CETL_NODISCARD static std::shared_ptr<GatewayImpl> create(ServerRouterImpl& router, const Endpoint& endpoint)
        {
            return std::make_shared<GatewayImpl>(Private(), router, endpoint);
        }

        GatewayImpl(Private, ServerRouterImpl& router, const Endpoint& endpoint)
            : router_{router}
            , endpoint_{endpoint}
            , next_sequence_{0}
        {
            router_.logger_->trace("Gateway(cl={}, tag={}).", endpoint.client_id, endpoint.tag);
        }

        GatewayImpl(const GatewayImpl&)                = delete;
        GatewayImpl(GatewayImpl&&) noexcept            = delete;
        GatewayImpl& operator=(const GatewayImpl&)     = delete;
        GatewayImpl& operator=(GatewayImpl&&) noexcept = delete;

        virtual ~GatewayImpl()
        {
            performWithoutThrowing([this] {
                //
                router_.logger_->trace("~Gateway(cl={}, tag={}, err={}).",
                                       endpoint_.client_id,
                                       endpoint_.tag,
                                       completion_opt_error_);

                router_.onGatewayDisposal(endpoint_, completion_opt_error_);
            });
        }

        // detail::Gateway

        CETL_NODISCARD sdk::OptError send(const detail::ServiceDesc::Id service_id, ListOfPayloads&& payloads) override
        {
            if (!router_.isConnected(endpoint_))
            {
                return sdk::Error{sdk::Error::Code::NotConnected};
            }
            if (!router_.isRegisteredGateway(endpoint_))
            {
                return sdk::Error{sdk::Error::Code::Shutdown};
            }

            Route_0_2 route{&router_.memory_};

            auto& channel_msg        = route.set_channel_msg();
            channel_msg.tag          = endpoint_.tag;
            channel_msg.sequence     = next_sequence_++;
            channel_msg.service_id   = service_id;
            channel_msg.payload_size = payloads.size();  // FIX: should be total size of all spans

            return tryPerformOnSerialized(route, [this, pays = std::move(payloads)](const auto prefix) mutable {
                //
                pays.push_front(prefix);
                return router_.server_pipe_->send(endpoint_.client_id, std::move(pays));
            });
        }

        CETL_NODISCARD sdk::OptError complete(const sdk::OptError opt_error, const bool keep_alive) override
        {
            if (!router_.isConnected(endpoint_))
            {
                return sdk::Error{sdk::Error::Code::NotConnected};
            }
            if (!router_.isRegisteredGateway(endpoint_))
            {
                return sdk::Error{sdk::Error::Code::Shutdown};
            }

            completion_opt_error_ = opt_error;

            Route_0_2 route{&router_.memory_};
            auto&     channel_end  = route.set_channel_end();
            channel_end.tag        = endpoint_.tag;
            channel_end.keep_alive = keep_alive;
            optErrorToDsdlError(opt_error, channel_end._error);

            return tryPerformOnSerialized(route, [this](const auto payload) {
                //
                return router_.server_pipe_->send(endpoint_.client_id, {{payload}});
            });
        }

        CETL_NODISCARD sdk::OptError event(const Event::Var& event) override
        {
            // It's fine to be not subscribed to events.
            return (event_handler_) ? event_handler_(event) : sdk::OptError{};
        }

        void subscribe(EventHandler event_handler) override
        {
            event_handler_ = std::move(event_handler);
            router_.onGatewaySubscription(endpoint_);
        }

    private:
        ServerRouterImpl& router_;
        const Endpoint    endpoint_;
        std::uint64_t     next_sequence_;
        EventHandler      event_handler_;
        sdk::OptError     completion_opt_error_;

    };  // GatewayImpl

    // Lifetime of a gateway is strictly managed by its channel. But router needs to "weakly" keep track of them.
    using MapOfWeakGateways         = std::unordered_map<Endpoint::Tag, detail::Gateway::WeakPtr>;
    using ClientIdToMapOfGateways   = std::unordered_map<Endpoint::ClientId, MapOfWeakGateways>;
    using ServiceIdToChannelFactory = std::unordered_map<detail::ServiceDesc::Id, TypeErasedChannelFactory>;

    CETL_NODISCARD bool isConnected(const Endpoint& endpoint) const noexcept
    {
        const auto cl_to_gws = client_id_to_map_of_gateways_.find(endpoint.client_id);
        return cl_to_gws != client_id_to_map_of_gateways_.end();
    }

    CETL_NODISCARD bool isRegisteredGateway(const Endpoint& endpoint) const noexcept
    {
        const auto cl_to_gws = client_id_to_map_of_gateways_.find(endpoint.client_id);
        if (cl_to_gws != client_id_to_map_of_gateways_.end())
        {
            const auto& map_of_gws = cl_to_gws->second;
            return map_of_gws.find(endpoint.tag) != map_of_gws.end();
        }
        return false;
    }

    template <typename Action>
    CETL_NODISCARD sdk::OptError findAndActOnRegisteredGateway(const Endpoint endpoint, Action&& action) const
    {
        const auto cl_to_gws = client_id_to_map_of_gateways_.find(endpoint.client_id);
        if (cl_to_gws != client_id_to_map_of_gateways_.end())
        {
            const auto& map_of_gws = cl_to_gws->second;

            const auto tag_to_gw = map_of_gws.find(endpoint.tag);
            if (tag_to_gw != map_of_gws.end())
            {
                if (const auto gateway = tag_to_gw->second.lock())
                {
                    return std::forward<Action>(action)(*gateway, tag_to_gw);
                }
            }
        }

        // It's fine and expected to have no gateway registered for the given endpoint.
        return sdk::OptError{};
    }

    void onGatewaySubscription(const Endpoint endpoint) const
    {
        if (isConnected(endpoint))
        {
            const auto opt_error = findAndActOnRegisteredGateway(endpoint, [](auto& gateway, auto) {
                //
                return gateway.event(detail::Gateway::Event::Connected{});
            });
            (void) opt_error;  // Best efforts strategy.
        }
    }

    /// Unregisters the gateway associated with the given endpoint.
    ///
    /// Called on the gateway disposal (correspondingly on its channel destruction).
    /// The "dying" gateway wishes to notify the remote client router about its disposal.
    /// This local router fulfills the wish if the gateway was registered and the client router is connected.
    ///
    void onGatewayDisposal(const Endpoint& endpoint, const sdk::OptError completion_opt_error)
    {
        const auto cl_to_gws = client_id_to_map_of_gateways_.find(endpoint.client_id);
        if (cl_to_gws != client_id_to_map_of_gateways_.end())
        {
            auto&      map_of_gws     = cl_to_gws->second;
            const bool was_registered = (map_of_gws.erase(endpoint.tag) > 0);

            // Notify remote client router about the gateway disposal (aka channel completion).
            // The router will propagate "ChEnd" event to the counterpart gateway (if it's registered).
            //
            if (was_registered && isConnected(endpoint))
            {
                Route_0_2 route{&memory_};
                auto&     channel_end  = route.set_channel_end();
                channel_end.tag        = endpoint.tag;
                channel_end.keep_alive = false;
                optErrorToDsdlError(completion_opt_error, channel_end._error);

                const auto opt_error = tryPerformOnSerialized(route, [this, &endpoint](const auto payload) {
                    //
                    return server_pipe_->send(endpoint.client_id, {{payload}});
                });
                // Best efforts strategy - gateway anyway is gone, so nowhere to report.
                (void) opt_error;
            }
        }
    }

    CETL_NODISCARD sdk::OptError handlePipeEvent(const pipe::ServerPipe::Event::Connected& pipe_conn) const
    {
        logger_->debug("Pipe is connected (cl={}).", pipe_conn.client_id);

        // It's not enough to consider the client router connected by the pipe event.
        // We gonna wait for `RouteConnect` negotiation (see `handleRouteConnect`).
        // But for now everything is fine.
        return sdk::OptError{};
    }

    CETL_NODISCARD sdk::OptError handlePipeEvent(const pipe::ServerPipe::Event::Message& msg)
    {
        Route_0_2  route_msg{&memory_};
        const auto result_size = tryDeserializePayload(msg.payload, route_msg);
        if (!result_size.has_value())
        {
            // Invalid message payload.
            return sdk::Error{sdk::Error::Code::InvalidArgument};
        }

        return cetl::visit(         //
            cetl::make_overloaded(  //
                [this](const uavcan::primitive::Empty_1_0&) {
                    //
                    // Unexpected message, but we can't remove it
                    // b/c Nunavut generated code needs a default case.
                    return sdk::OptError{sdk::Error{sdk::Error::Code::InvalidArgument}};
                },
                [this, &msg](const RouteConnect_0_1& route_conn) {
                    //
                    return handleRouteConnect(msg.client_id, route_conn);
                },
                [this, &msg](const RouteChannelMsg_0_1& route_ch_msg) {
                    //
                    return handleRouteChannelMsg(msg.client_id, route_ch_msg, msg.payload);
                },
                [this, &msg](const RouteChannelEnd_0_2& route_ch_end) {
                    //
                    return handleRouteChannelEnd(msg.client_id, route_ch_end);
                }),
            route_msg.union_value);
    }

    CETL_NODISCARD sdk::OptError handlePipeEvent(const pipe::ServerPipe::Event::Disconnected& disconn)
    {
        logger_->debug("Pipe is disconnected (cl={}).", disconn.client_id);

        const auto cl_to_gws = client_id_to_map_of_gateways_.find(disconn.client_id);
        if (cl_to_gws != client_id_to_map_of_gateways_.end())
        {
            const auto local_map_of_gateways = std::move(cl_to_gws->second);
            client_id_to_map_of_gateways_.erase(cl_to_gws);

            const sdk::OptError                     disconnected_error{sdk::Error{sdk::Error::Code::Disconnected}};
            const detail::Gateway::Event::Completed completed{disconnected_error, false};

            // The whole client router is disconnected, so we need to unregister and notify all its gateways.
            //
            for (const auto& tag_to_gw : local_map_of_gateways)
            {
                if (const auto gateway = tag_to_gw.second.lock())
                {
                    const auto opt_error = gateway->event(completed);
                    (void) opt_error;  // Best efforts strategy.
                }
            }
        }

        // It's fine for a client to be already disconnected.
        return sdk::OptError{};
    }

    CETL_NODISCARD sdk::OptError handleRouteConnect(const pipe::ServerPipe::ClientId client_id,
                                                    const RouteConnect_0_1&          rt_conn)
    {
        logger_->debug("Route connect request (cl={}, ver='{}.{}', err_code={}).",
                       client_id,
                       static_cast<int>(rt_conn.version.major),
                       static_cast<int>(rt_conn.version.minor),
                       rt_conn._error.error_code);

        Route_0_2 route{&memory_};
        auto&     route_conn     = route.set_connect();
        route_conn.version.major = VERSION_MAJOR;
        route_conn.version.minor = VERSION_MINOR;
        // In the future, we might have version comparison logic here,
        // and potentially refuse the connection if the versions are incompatible.
        optErrorToDsdlError(sdk::OptError{}, route_conn._error);

        const auto opt_error = tryPerformOnSerialized(route, [this, client_id](const auto payload) {
            //
            return server_pipe_->send(client_id, {{payload}});
        });
        if (!opt_error)
        {
            client_id_to_map_of_gateways_.insert({client_id, MapOfWeakGateways{}});
        }
        return opt_error;
    }

    CETL_NODISCARD sdk::OptError handleRouteChannelMsg(const pipe::ServerPipe::ClientId client_id,
                                                       const RouteChannelMsg_0_1&       route_ch_msg,
                                                       const Payload                    payload)
    {
        // Cut routing stuff from the payload - remaining is the real message payload.
        const auto msg_real_payload = payload.subspan(payload.size() - route_ch_msg.payload_size);

        const auto cl_to_gws = client_id_to_map_of_gateways_.find(client_id);
        if (cl_to_gws != client_id_to_map_of_gateways_.end())
        {
            auto& map_of_gws = cl_to_gws->second;

            const auto tag_to_gw = map_of_gws.find(route_ch_msg.tag);
            if (tag_to_gw != map_of_gws.end())
            {
                if (auto gateway = tag_to_gw->second.lock())
                {
                    logger_->trace("Route Ch Msg (cl={}, tag={}, seq={}).",
                                   client_id,
                                   route_ch_msg.tag,
                                   route_ch_msg.sequence);

                    return gateway->event(detail::Gateway::Event::Message{route_ch_msg.sequence, msg_real_payload});
                }
            }

            // Only the very first message in the sequence is considered to trigger channel factory.
            if (route_ch_msg.sequence == 0)
            {
                const auto si_to_ch_factory = service_id_to_channel_factory_.find(route_ch_msg.service_id);
                if (si_to_ch_factory != service_id_to_channel_factory_.end())
                {
                    const Endpoint endpoint{route_ch_msg.tag, client_id};

                    auto gateway                 = GatewayImpl::create(*this, endpoint);
                    map_of_gws[route_ch_msg.tag] = gateway;

                    logger_->debug("Route Ch Msg (cl={}, tag={}, seq={}, srv=0x{:X}).",
                                   client_id,
                                   route_ch_msg.tag,
                                   route_ch_msg.sequence,
                                   route_ch_msg.service_id);

                    si_to_ch_factory->second(gateway, msg_real_payload);
                    return sdk::OptError{};
                }
            }
        }

        // Nothing to do here with unsolicited messages - just trace and ignore them.
        //
        logger_->debug("Route Ch Unsolicited Msg (cl={}, tag={}, seq={}, srv=0x{:X}).",
                       client_id,
                       route_ch_msg.tag,
                       route_ch_msg.sequence,
                       route_ch_msg.service_id);
        return sdk::OptError{};
    }

    CETL_NODISCARD sdk::OptError handleRouteChannelEnd(const pipe::ServerPipe::ClientId client_id,
                                                       const RouteChannelEnd_0_2&       route_ch_end)
    {
        const auto keep_alive = route_ch_end.keep_alive;
        const auto opt_error  = dsdlErrorToOptError(route_ch_end._error);

        logger_->debug("Route Ch End (cl={}, tag={}, keep_alive={}, err={}).",
                       client_id,
                       route_ch_end.tag,
                       keep_alive,
                       opt_error);

        const auto cl_to_gws = client_id_to_map_of_gateways_.find(client_id);
        if (cl_to_gws != client_id_to_map_of_gateways_.end())
        {
            auto&                                   map_of_gws = cl_to_gws->second;
            const detail::Gateway::Event::Completed completed{opt_error, keep_alive};

            const Endpoint endpoint{route_ch_end.tag, client_id};
            return findAndActOnRegisteredGateway(  //
                endpoint,
                [this, keep_alive, &completed, &map_of_gws](auto& gateway, auto found_it) {
                    //
                    if (!keep_alive)
                    {
                        map_of_gws.erase(found_it);
                    }

                    return gateway.event(completed);
                });
        }

        // It's fine for a client to be already disconnected.
        return sdk::OptError{};
    }

    cetl::pmr::memory_resource& memory_;
    pipe::ServerPipe::Ptr       server_pipe_;
    LoggerPtr                   logger_;
    ClientIdToMapOfGateways     client_id_to_map_of_gateways_;
    ServiceIdToChannelFactory   service_id_to_channel_factory_;

};  // ClientRouterImpl

}  // namespace

ServerRouter::Ptr ServerRouter::make(cetl::pmr::memory_resource& memory, pipe::ServerPipe::Ptr server_pipe)
{
    return std::make_unique<ServerRouterImpl>(memory, std::move(server_pipe));
}

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd
