//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_GATEWAY_MOCK_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_GATEWAY_MOCK_HPP_INCLUDED

#include "ipc/gateway.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "ref_wrapper.hpp"

#include <gmock/gmock.h>

namespace ocvsmd
{
namespace common
{
namespace ipc
{
namespace detail
{

class GatewayMock : public Gateway
{
public:
    struct Wrapper final : RefWrapper<Gateway, GatewayMock>
    {
        using RefWrapper::RefWrapper;

        // MARK: Gateway

        CETL_NODISCARD sdk::OptErrorCode send(const ServiceDesc::Id service_id, const Payload payload) override
        {
            return reference().send(service_id, payload);
        }

        CETL_NODISCARD sdk::OptErrorCode complete(const sdk::OptErrorCode error_code, const bool keep_alive) override
        {
            return reference().complete(error_code, keep_alive);
        }

        CETL_NODISCARD sdk::OptErrorCode event(const Event::Var& event) override
        {
            return reference().event(event);
        }

        void subscribe(EventHandler event_handler) override
        {
            reference().event_handler_ = event_handler;
            reference().subscribe(event_handler);
        }

    };  // Wrapper

    MOCK_METHOD(void, deinit, (), (const));
    MOCK_METHOD(sdk::OptErrorCode, send, (const ServiceDesc::Id service_id, const Payload payload), (override));
    MOCK_METHOD(sdk::OptErrorCode, complete, (const sdk::OptErrorCode error_code, const bool keep_alive), (override));
    MOCK_METHOD(sdk::OptErrorCode, event, (const Event::Var& event), (override));
    MOCK_METHOD(void, subscribe, (EventHandler event_handler), (override));

    // NOLINTBEGIN
    EventHandler event_handler_;
    // NOLINTEND

};  // GatewayMock

}  // namespace detail
}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_GATEWAY_MOCK_HPP_INCLUDED
