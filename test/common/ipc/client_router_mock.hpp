//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_CLIENT_ROUTER_MOCK_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_CLIENT_ROUTER_MOCK_HPP_INCLUDED

#include "ipc/client_router.hpp"

#include "gateway_mock.hpp"
#include "ocvsmd/sdk/defines.hpp"

#include <gmock/gmock.h>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

class ClientRouterMock : public ClientRouter
{
public:
    explicit ClientRouterMock(cetl::pmr::memory_resource& memory)
        : memory_{memory}
    {
    }

    // ClientRouter

    MOCK_METHOD(sdk::OptError, start, (), (override));
    MOCK_METHOD(detail::Gateway::Ptr, makeGateway, (), (override));

    cetl::pmr::memory_resource& memory() override
    {
        return memory_;
    }

    // MARK: Data members:

    // NOLINTBEGIN
    cetl::pmr::memory_resource& memory_;
    // NOLINTEND

};  // ClientRouterMock

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_CLIENT_ROUTER_MOCK_HPP_INCLUDED
