//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "svc/relay/create_raw_sub_service.hpp"

#include "common/common_gtest_helpers.hpp"
#include "common/ipc/gateway_mock.hpp"
#include "common/ipc/ipc_gtest_helpers.hpp"
#include "common/ipc/server_router_mock.hpp"
#include "daemon/engine/cyphal/msg_sessions_mock.hpp"
#include "daemon/engine/cyphal/transport_gtest_helpers.hpp"
#include "daemon/engine/cyphal/transport_mock.hpp"
#include "ipc/channel.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "svc/relay/create_raw_sub_spec.hpp"
#include "svc/relay/services.hpp"
#include "svc/svc_helpers.hpp"
#include "tracking_memory_resource.hpp"
#include "virtual_time_scheduler.hpp"

#include <libcyphal/errors.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace
{

using namespace ocvsmd::common;               // NOLINT This our main concern here in the unit tests.
using namespace ocvsmd::daemon::engine::svc;  // NOLINT This our main concern here in the unit tests.
using ocvsmd::sdk::Error;
using ocvsmd::sdk::OptError;

using testing::_;
using testing::Invoke;
using testing::IsNull;
using testing::Return;
using testing::IsEmpty;
using testing::NotNull;
using testing::StrictMock;

// https://github.com/llvm/llvm-project/issues/53444
// NOLINTBEGIN(misc-unused-using-decls, misc-include-cleaner)
using std::literals::chrono_literals::operator""s;
using std::literals::chrono_literals::operator""ms;
// NOLINTEND(misc-unused-using-decls, misc-include-cleaner)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

class TestCreateRawSubService : public testing::Test
{
protected:
    using Spec         = svc::relay::CreateRawSubSpec;
    using GatewayMock  = ipc::detail::GatewayMock;
    using GatewayEvent = ipc::detail::Gateway::Event;

    using CyPresentation       = libcyphal::presentation::Presentation;
    using CyProtocolParams     = libcyphal::transport::ProtocolParams;
    using CyMsgRxTransfer      = libcyphal::transport::MessageRxTransfer;
    using CyMsgRxSessionMock   = StrictMock<libcyphal::transport::MessageRxSessionMock>;
    using CyUniquePtrMsgRxSpec = CyMsgRxSessionMock::RefWrapper::Spec;
    struct CyMsgSessions
    {
        CyMsgRxSessionMock                              msg_rx_mock;
        CyMsgRxSessionMock::OnReceiveCallback::Function msg_rx_cb_fn;
    };

    void SetUp() override
    {
        cetl::pmr::set_default_resource(&mr_);

        EXPECT_CALL(cy_transport_mock_, getProtocolParams())
            .WillRepeatedly(
                Return(CyProtocolParams{std::numeric_limits<libcyphal::transport::TransferId>::max(), 0, 0}));
    }

    void TearDown() override
    {
        EXPECT_THAT(mr_.allocations, IsEmpty());
        EXPECT_THAT(mr_.total_allocated_bytes, mr_.total_deallocated_bytes);
    }

    libcyphal::TimePoint now() const
    {
        return scheduler_.now();
    }

    // NOLINTBEGIN
    ocvsmd::TrackingMemoryResource                  mr_;
    ocvsmd::VirtualTimeScheduler                    scheduler_{};
    StrictMock<libcyphal::transport::TransportMock> cy_transport_mock_;
    StrictMock<ipc::ServerRouterMock>               ipc_router_mock_{mr_};
    const std::string                               svc_name_{Spec::svc_full_name()};
    const ipc::detail::ServiceDesc svc_desc_{ipc::AnyChannel::getServiceDesc<Spec::Request>(svc_name_)};

    // NOLINTEND
};

// MARK: - Tests:

TEST_F(TestCreateRawSubService, registerWithContext)
{
    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_THAT(ipc_router_mock_.getChannelFactory(svc_desc_), IsNull());

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(svc_name_)).WillOnce(Return());
    relay::CreateRawSubService::registerWithContext(svc_context);

    EXPECT_THAT(ipc_router_mock_.getChannelFactory(svc_desc_), NotNull());
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

}  // namespace
