//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "svc/relay/create_raw_sub_service.hpp"

#include "common/ipc/gateway_mock.hpp"
#include "common/ipc/ipc_gtest_helpers.hpp"
#include "common/ipc/server_router_mock.hpp"
#include "daemon/engine/cyphal/msg_sessions_mock.hpp"
#include "daemon/engine/cyphal/scattered_buffer_storage_mock.hpp"
#include "daemon/engine/cyphal/transport_gtest_helpers.hpp"
#include "daemon/engine/cyphal/transport_mock.hpp"
#include "ipc/channel.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "svc/relay/create_raw_sub_spec.hpp"
#include "svc/svc_helpers.hpp"
#include "tracking_memory_resource.hpp"
#include "virtual_time_scheduler.hpp"

#include <uavcan/node/Version_1_0.hpp>

#include <cetl/pf17/cetlpf.hpp>

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
using testing::NiceMock;
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

    using CyTestMessage = uavcan::node::Version_1_0;

    using CyPortId                     = libcyphal::transport::PortId;
    using CyPresentation               = libcyphal::presentation::Presentation;
    using CyMsgRxTransfer              = libcyphal::transport::MessageRxTransfer;
    using CyProtocolParams             = libcyphal::transport::ProtocolParams;
    using CyMsgRxSessionMock           = StrictMock<libcyphal::transport::MessageRxSessionMock>;
    using CyUniquePtrMsgRxSpec         = CyMsgRxSessionMock::RefWrapper::Spec;
    using CyScatteredBuffer            = libcyphal::transport::ScatteredBuffer;
    using CyScatteredBufferStorageMock = libcyphal::transport::ScatteredBufferStorageMock;

    struct CySessCntx
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

    void expectCyMsgSession(CySessCntx& cy_sess_cntx, const CyPortId subject_id)
    {
        const libcyphal::transport::MessageRxParams rx_params{CyTestMessage::_traits_::ExtentBytes, subject_id};

        EXPECT_CALL(cy_sess_cntx.msg_rx_mock, getParams())  //
            .WillOnce(Return(rx_params));
        EXPECT_CALL(cy_sess_cntx.msg_rx_mock, setOnReceiveCallback(_))  //
            .WillRepeatedly(Invoke([&](auto&& cb_fn) {                  //
                cy_sess_cntx.msg_rx_cb_fn = std::forward<decltype(cb_fn)>(cb_fn);
            }));
        EXPECT_CALL(cy_transport_mock_, makeMessageRxSession(MessageRxParamsEq(rx_params)))  //
            .WillOnce(Invoke([&](const auto&) {                                              //
                return libcyphal::detail::makeUniquePtr<CyUniquePtrMsgRxSpec>(mr_, cy_sess_cntx.msg_rx_mock);
            }));
        EXPECT_CALL(cy_sess_cntx.msg_rx_mock, deinit()).Times(1);
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

TEST_F(TestCreateRawSubService, request)
{
    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(_)).WillOnce(Return());
    relay::CreateRawSubService::registerWithContext(svc_context);

    auto* const ch_factory = ipc_router_mock_.getChannelFactory(svc_desc_);
    ASSERT_THAT(ch_factory, NotNull());

    StrictMock<GatewayMock> gateway_mock;

    Spec::Request request{&mr_};
    request.extent_size = CyTestMessage::_traits_::ExtentBytes;
    request.subject_id  = 123;

    CySessCntx cy_sess_cntx;

    scheduler_.scheduleAt(1s, [&](const auto&) {
        //
        // Emulate service request.
        expectCyMsgSession(cy_sess_cntx, request.subject_id);
        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::make_shared<GatewayMock::Wrapper>(gateway_mock), payload);
            return OptError{};
        });
        EXPECT_THAT(result, OptError{});

        gateway_mock.event_handler_(GatewayEvent::Completed{OptError{}, true});
    });
    scheduler_.scheduleAt(2s, [&](const auto&) {
        //
        // Emulate that node 42 has published an empty raw message.
        //
        Spec::Response expected_response{&mr_};
        expected_response.priority = 4;
        expected_response.remote_node_id.push_back(42);
        EXPECT_CALL(gateway_mock, send(_, ipc::PayloadWith<Spec::Response>(mr_, expected_response))).Times(1);
        //
        CyMsgRxTransfer transfer{{{{0, libcyphal::transport::Priority::Nominal}, now()}, 42}, {}};
        cy_sess_cntx.msg_rx_cb_fn({transfer});
    });
    scheduler_.scheduleAt(3s, [&](const auto&) {
        //
        // Emulate that anonymous node has published a raw message (3 bytes).
        //
        Spec::Response expected_response{&mr_};
        expected_response.priority     = 5;
        expected_response.payload_size = 3;
        EXPECT_CALL(gateway_mock, send(_, ipc::PayloadWith<Spec::Response>(mr_, expected_response))).Times(1);
        //
        NiceMock<CyScatteredBufferStorageMock> storage_mock;
        EXPECT_CALL(storage_mock, size()).WillRepeatedly(Return(3));
        CyScatteredBufferStorageMock::Wrapper storage{&storage_mock};
        CyMsgRxTransfer transfer{{{{147, libcyphal::transport::Priority::Low}, now()}, cetl::nullopt},
                                 CyScatteredBuffer{std::move(storage)}};
        cy_sess_cntx.msg_rx_cb_fn({transfer});
    });
    scheduler_.scheduleAt(9s, [&](const auto&) {
        //
        EXPECT_CALL(gateway_mock, complete(OptError{Error{Error::Code::Canceled}}, false)).Times(1);
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
        gateway_mock.event_handler_(GatewayEvent::Completed{OptError{}, false});
    });
    scheduler_.scheduleAt(9s + 1ms, [&](const auto&) {
        //
        testing::Mock::VerifyAndClearExpectations(&gateway_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_cntx.msg_rx_mock);
    });
    scheduler_.spinFor(10s);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

}  // namespace

namespace ocvsmd
{
namespace common
{
namespace svc
{
namespace relay
{
namespace CreateRawSub
{
static void PrintTo(const Response_0_1& res, std::ostream* os)  // NOLINT
{
    const auto node_id = res.remote_node_id.empty() ? 65535 : res.remote_node_id.front();
    *os << "CreateRawSub::Response_0_1{priority=" << static_cast<int>(res.priority) << ", node_id=" << node_id
        << ", payload_size=" << res.payload_size << "}";
}
static bool operator==(const Response_0_1& lhs, const Response_0_1& rhs)  // NOLINT
{
    return (lhs.priority == rhs.priority) && (lhs.remote_node_id == rhs.remote_node_id) &&
           (lhs.payload_size == rhs.payload_size);
}
}  // namespace CreateRawSub
}  // namespace relay
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd
