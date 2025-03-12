//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include <ocvsmd/sdk/node_registry_client.hpp>

#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "sdk_factory.hpp"
#include "svc/as_sender.hpp"
#include "svc/node/access_registers_client.hpp"
#include "svc/node/access_registers_spec.hpp"
#include "svc/node/list_registers_client.hpp"
#include "svc/node/list_registers_spec.hpp"

#include <uavcan/_register/Name_1_0.hpp>

#include <cetl/pf17/cetlpf.hpp>

#include <algorithm>
#include <cstdint>

namespace ocvsmd
{
namespace sdk
{
namespace
{

class NodeRegistryClientImpl final : public NodeRegistryClient
{
public:
    NodeRegistryClientImpl(cetl::pmr::memory_resource& memory, common::ipc::ClientRouter::Ptr ipc_router)
        : memory_{memory}
        , ipc_router_{std::move(ipc_router)}
        , logger_{common::getLogger("sdk")}
    {
    }

    // NodeRegistryClient

    SenderOf<List::Result>::Ptr list(const CyphalNodeIds node_ids, const std::chrono::microseconds timeout) override
    {
        using ListRegistersClient = svc::node::ListRegistersClient;
        using Request             = common::svc::node::ListRegistersSpec::Request;

        logger_->trace("NodeRegistryClient: Making sender of `list()`.");

        Request request{std::max<std::uint64_t>(0, timeout.count()),
                        {node_ids.begin(), node_ids.end(), &memory_},
                        &memory_};
        auto    svc_client = ListRegistersClient::make(ipc_router_, std::move(request));

        return std::make_unique<svc::AsSender<ListRegistersClient, ListRegistersClient::Result>>(  //
            "NodeRegistryClient::list",
            std::move(svc_client),
            logger_);
    }

    SenderOf<Access::Result>::Ptr read(const CyphalNodeIds                       node_ids,
                                       const cetl::span<const cetl::string_view> registers,
                                       const std::chrono::microseconds           timeout) override
    {
        using RegKey                = uavcan::_register::Name_1_0;
        using AccessRegistersClient = svc::node::AccessRegistersClient;

        logger_->trace("NodeRegistryClient: Making sender of `read()`.");

        for (const auto& reg_key : registers)
        {
            if (reg_key.size() > RegKey::_traits_::ArrayCapacity::name)
            {
                logger_->error("Too long register key '{}'.", reg_key);
                return just<Access::Result>(ErrorCode::InvalidArgument);
            }
        }

        auto svc_client = AccessRegistersClient::make(memory_, ipc_router_, node_ids, registers, timeout);

        return std::make_unique<svc::AsSender<AccessRegistersClient, AccessRegistersClient::Result>>(  //
            "NodeRegistryClient::read",
            std::move(svc_client),
            logger_);
    }

    SenderOf<Access::Result>::Ptr write(const CyphalNodeIds                         node_ids,
                                        const cetl::span<const Access::RegKeyValue> registers,
                                        const std::chrono::microseconds             timeout) override
    {
        using RegKey                = uavcan::_register::Name_1_0;
        using AccessRegistersClient = svc::node::AccessRegistersClient;

        logger_->trace("NodeRegistryClient: Making sender of `read()`.");

        for (const auto& reg : registers)
        {
            if (reg.key.size() > RegKey::_traits_::ArrayCapacity::name)
            {
                logger_->error("Too long register key '{}'.", reg.key);
                return just<Access::Result>(ErrorCode::InvalidArgument);
            }
        }

        auto svc_client = AccessRegistersClient::make(memory_, ipc_router_, node_ids, registers, timeout);

        return std::make_unique<svc::AsSender<AccessRegistersClient, AccessRegistersClient::Result>>(  //
            "NodeRegistryClient::write",
            std::move(svc_client),
            logger_);
    }

private:
    cetl::pmr::memory_resource&    memory_;
    common::LoggerPtr              logger_;
    common::ipc::ClientRouter::Ptr ipc_router_;

};  // NodeRegistryClientImpl

}  // namespace

CETL_NODISCARD NodeRegistryClient::Ptr Factory::makeNodeRegistryClient(cetl::pmr::memory_resource&    memory,
                                                                       common::ipc::ClientRouter::Ptr ipc_router)
{
    return std::make_shared<NodeRegistryClientImpl>(memory, std::move(ipc_router));
}

}  // namespace sdk
}  // namespace ocvsmd
