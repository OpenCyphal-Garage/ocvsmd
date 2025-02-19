//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "engine.hpp"

#include "config.hpp"
#include "cyphal/can_transport_bag.hpp"
#include "cyphal/file_provider.hpp"
#include "cyphal/udp_transport_bag.hpp"
#include "engine_helpers.hpp"
#include "io/socket_address.hpp"
#include "ipc/pipe/server_pipe.hpp"
#include "ipc/pipe/socket_server.hpp"
#include "ipc/server_router.hpp"
#include "svc/file_server/services.hpp"
#include "svc/node/services.hpp"
#include "svc/svc_helpers.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/application/node.hpp>
#include <libcyphal/types.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <utility>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{

Engine::Engine(Config::Ptr config)
    : config_{std::move(config)}
{
}

cetl::optional<std::string> Engine::init()
{
    logger_->trace("Initializing engine...");

    // 1. Create the transport layer object (try first UDP, then CAN).
    //    Set the local node ID if configured.
    //
    if (auto maybe_udp_transport_bag = cyphal::UdpTransportBag::make(memory_, executor_, config_))
    {
        any_transport_bag_ = std::move(maybe_udp_transport_bag);
    }
    else
    {
        if (auto maybe_can_transport_bag = cyphal::CanTransportBag::make(memory_, executor_, config_))
        {
            any_transport_bag_ = std::move(maybe_can_transport_bag);
        }
        else
        {
            std::string msg = "Failed to create Cyphal transport.";
            logger_->error(msg);
            return msg;
        }
    }
    if (const auto node_id = config_->getCyphalAppNodeId())
    {
        any_transport_bag_->getTransport().setLocalNodeId(node_id.value());
    }

    // 2. Create the presentation layer object.
    //
    presentation_.emplace(memory_, executor_, any_transport_bag_->getTransport());
    presentation_->setTransferIdMap(&transfer_id_map_);

    // 3. Create the node object with name.
    //
    auto maybe_node = libcyphal::application::Node::make(*presentation_);
    if (const auto* failure = cetl::get_if<libcyphal::application::Node::MakeFailure>(&maybe_node))
    {
        (void) failure;
        std::string msg = "Failed to create cyphal node.";
        logger_->error(msg);
        return msg;
    }
    node_.emplace(cetl::get<libcyphal::application::Node>(std::move(maybe_node)));

    // 4. Populate the node info.
    //
    auto& get_info_prov = node_->getInfoProvider();
    get_info_prov  //
        .setName(NODE_NAME)
        .setSoftwareVersion(VERSION_MAJOR, VERSION_MINOR)
        .setSoftwareVcsRevisionId(VCS_REVISION_ID)
        .setUniqueId(getUniqueId());

    // 5. Bring up various providers.
    //
    file_provider_ = cyphal::FileProvider::make(memory_, *presentation_, config_);
    if (file_provider_ == nullptr)
    {
        std::string msg = "Failed to create cyphal file provider.";
        logger_->error(msg);
        return msg;
    }

    // 6. Bring up the IPC router and its services.
    //
    common::ipc::pipe::ServerPipe::Ptr server_pipe;
    {
        using ParseResult = common::io::SocketAddress::ParseResult;

        auto ipc_connections = config_->getIpcConnections();
        if (ipc_connections.empty())
        {
            std::string msg = "No IPC connections configured.";
            logger_->error(msg);
            return msg;
        }

        logger_->debug("Starting with IPC connection '{}'...", ipc_connections.front());
        auto maybe_socket_address = common::io::SocketAddress::parse(ipc_connections.front(), 0);
        if (const auto* const failure = cetl::get_if<ParseResult::Failure>(&maybe_socket_address))
        {
            (void) failure;
            std::string msg = "Failed to parse IPC connection.";
            logger_->error(msg);
            return msg;
        }
        const auto socket_address = cetl::get<ParseResult::Success>(maybe_socket_address);
        server_pipe               = std::make_unique<common::ipc::pipe::SocketServer>(executor_, socket_address);
    }
    //
    ipc_router_ = common::ipc::ServerRouter::make(memory_, std::move(server_pipe));
    //
    const svc::ScvContext svc_context{memory_, executor_, *ipc_router_, *presentation_};
    svc::node::registerAllServices(svc_context);
    svc::file_server::registerAllServices(svc_context, *file_provider_);
    //
    if (0 != ipc_router_->start())
    {
        std::string msg = "Failed to start IPC router.";
        logger_->error(msg);
        return msg;
    }

    logger_->debug("Engine is initialized.");
    return cetl::nullopt;
}

void Engine::runWhile(const std::function<bool()>& loop_predicate)
{
    using std::chrono_literals::operator""s;

    libcyphal::Duration worst_lateness{0};
    while (loop_predicate())
    {
        const auto spin_result = executor_.spinOnce();
        worst_lateness         = std::max(worst_lateness, spin_result.worst_lateness);

        // Poll awaitable resources but awake at least once per second.
        libcyphal::Duration timeout{1s};
        if (spin_result.next_exec_time.has_value())
        {
            timeout = std::min(timeout, spin_result.next_exec_time.value() - executor_.now());
        }

        if (const auto poll_failure = executor_.pollAwaitableResourcesFor(cetl::make_optional(timeout)))
        {
            spdlog::warn("Failed to poll awaitable resources (err={}).", failureToErrorCode(*poll_failure));
        }
    }
    spdlog::debug("Run loop predicate is fulfilled (worst_lateness={}us).",
                  std::chrono::duration_cast<std::chrono::microseconds>(worst_lateness).count());
}

Engine::UniqueId Engine::getUniqueId() const
{
    if (const auto unique_id = config_->getCyphalAppUniqueId())
    {
        return unique_id.value();
    }

    UniqueId out_unique_id = {};

    std::random_device                          rd;         // Seed for the random number engine
    std::mt19937                                gen{rd()};  // Mersenne Twister engine
    std::uniform_int_distribution<std::uint8_t> dis{std::numeric_limits<std::uint8_t>::min(),
                                                    std::numeric_limits<std::uint8_t>::max()};

    // Populate the default; it is only used at the first run.
    for (auto& b : out_unique_id)
    {
        b = dis(gen);
    }

    config_->setCyphalAppUniqueId(out_unique_id);
    config_->save();

    return out_unique_id;
}

}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
