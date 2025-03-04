//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include <ocvsmd/sdk/node_registry_client.hpp>

#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "sdk_factory.hpp"

#include <cetl/pf17/cetlpf.hpp>

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

    SenderOf<List::Result>::Ptr list(const cetl::span<const std::uint16_t> node_ids,
                                     const std::chrono::microseconds       timeout) override
    {
        return nullptr;
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
