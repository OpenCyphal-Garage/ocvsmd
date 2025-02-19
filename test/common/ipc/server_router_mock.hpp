//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_SERVER_ROUTER_MOCK_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_SERVER_ROUTER_MOCK_HPP_INCLUDED

#include "ipc/server_router.hpp"

#include <gmock/gmock.h>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

class ServerRouterMock : public ServerRouter
{
public:
    using ServiceIdToChannelFactory = std::unordered_map<detail::ServiceDesc::Id, TypeErasedChannelFactory>;

    explicit ServerRouterMock(cetl::pmr::memory_resource& memory)
        : memory_{memory}
    {
    }

    MOCK_METHOD(void, registerChannelFactoryByName, (const std::string& service_name), (const));

    TypeErasedChannelFactory* getChannelFactory(const detail::ServiceDesc& svc_desc)
    {
        const auto it = service_id_to_channel_factory_.find(svc_desc.id);
        return (it != service_id_to_channel_factory_.end()) ? &it->second : nullptr;
    }

    // ServerRouter

    MOCK_METHOD(int, start, (), (override));

    cetl::pmr::memory_resource& memory() override
    {
        return memory_;
    }

    void registerChannelFactory(const detail::ServiceDesc service_desc,  //
                                TypeErasedChannelFactory  channel_factory) override
    {
        registerChannelFactoryByName(std::string{service_desc.name.data(), service_desc.name.size()});
        service_id_to_channel_factory_[service_desc.id] = std::move(channel_factory);
    }

    // MARK: Data members:

    // NOLINTBEGIN
    cetl::pmr::memory_resource& memory_;
    ServiceIdToChannelFactory   service_id_to_channel_factory_;
    // NOLINTEND

};  // ServerRouterMock

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_SERVER_ROUTER_MOCK_HPP_INCLUDED
