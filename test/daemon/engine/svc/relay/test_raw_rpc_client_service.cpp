//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "svc/relay/raw_rpc_client_service.hpp"

#include "common/io/io_gtest_helpers.hpp"
#include "common/ipc/gateway_mock.hpp"
#include "common/ipc/server_router_mock.hpp"
#include "daemon/engine/cyphal/scattered_buffer_storage_mock.hpp"
#include "daemon/engine/cyphal/svc_sessions_mock.hpp"
#include "daemon/engine/cyphal/transport_gtest_helpers.hpp"
#include "daemon/engine/cyphal/transport_mock.hpp"
#include "ipc/channel.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "svc/relay/raw_rpc_client_spec.hpp"
#include "svc/svc_helpers.hpp"
#include "tracking_memory_resource.hpp"
#include "verify_utilz.hpp"
#include "virtual_time_scheduler.hpp"

#include <uavcan/node/ExecuteCommand_1_3.hpp>

#include <cetl/pf17/cetlpf.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace
{

using namespace ocvsmd::common;               // NOLINT This our main concern here in the unit tests.
using namespace ocvsmd::daemon::engine::svc;  // NOLINT This our main concern here in the unit tests.
using ocvsmd::sdk::Error;
using ocvsmd::sdk::OptError;

using ocvsmd::verify_utilz::b;

using testing::_;
using testing::Invoke;
using testing::IsNull;
using testing::Return;
using testing::IsEmpty;
using testing::NotNull;
using testing::NiceMock;
using testing::StrictMock;
using testing::VariantWith;

// https://github.com/llvm/llvm-project/issues/53444
// NOLINTBEGIN(misc-unused-using-decls, misc-include-cleaner)
using std::literals::chrono_literals::operator""s;
using std::literals::chrono_literals::operator""ms;
using std::literals::chrono_literals::operator""us;
// NOLINTEND(misc-unused-using-decls, misc-include-cleaner)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

class TestRawRpcClientService : public testing::Test
{
protected:
    using Spec           = svc::relay::RawRpcClientSpec;
    using GatewayMock    = ipc::detail::GatewayMock;
    using GatewayEvent   = ipc::detail::Gateway::Event;
    using ErrorResponse  = Error_0_1;
    using EmptyResponse  = uavcan::primitive::Empty_1_0;
    using RawMsgResponse = svc::relay::RawRpcClientReceive_0_1;

    using CyPriority                   = libcyphal::transport::Priority;
    using CyService                    = uavcan::node::ExecuteCommand_1_3;
    using CyPresentation               = libcyphal::presentation::Presentation;
    using CySvcRxTransfer              = libcyphal::transport::ServiceRxTransfer;
    using CyProtocolParams             = libcyphal::transport::ProtocolParams;
    using CyRequestTxSessionMock       = StrictMock<libcyphal::transport::RequestTxSessionMock>;
    using CyResponseRxSessionMock      = StrictMock<libcyphal::transport::ResponseRxSessionMock>;
    using CyUniquePtrReqTxSpec         = CyRequestTxSessionMock::RefWrapper::Spec;
    using CyUniquePtrResRxSpec         = CyResponseRxSessionMock::RefWrapper::Spec;
    using CyScatteredBuffer            = libcyphal::transport::ScatteredBuffer;
    using CyScatteredBufferStorageMock = libcyphal::transport::ScatteredBufferStorageMock;

    struct CySessCntx
    {
        CyRequestTxSessionMock                               req_tx_mock;
        CyResponseRxSessionMock                              res_rx_mock;
        CyResponseRxSessionMock::OnReceiveCallback::Function res_rx_cb_fn;
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

    void expectCySvcSessions(CySessCntx& cy_sess_cntx, const libcyphal::transport::NodeId node_id)
    {
        const libcyphal::transport::RequestTxParams  tx_params{CyService::Request::_traits_::FixedPortId, node_id};
        const libcyphal::transport::ResponseRxParams rx_params{CyService::Response::_traits_::ExtentBytes,
                                                               tx_params.service_id,
                                                               tx_params.server_node_id};

        EXPECT_CALL(cy_transport_mock_, makeRequestTxSession(RequestTxParamsEq(tx_params)))  //
            .WillOnce(Invoke([&](const auto&) {                                              //
                return libcyphal::detail::makeUniquePtr<CyUniquePtrReqTxSpec>(mr_, cy_sess_cntx.req_tx_mock);
            }));
        EXPECT_CALL(cy_sess_cntx.req_tx_mock, deinit()).Times(1);

        EXPECT_CALL(cy_sess_cntx.res_rx_mock, getParams())  //
            .WillOnce(Return(rx_params));
        EXPECT_CALL(cy_sess_cntx.res_rx_mock, setTransferIdTimeout(_))  //
            .WillOnce(Return());
        EXPECT_CALL(cy_sess_cntx.res_rx_mock, setOnReceiveCallback(_))  //
            .WillRepeatedly(Invoke([&](auto&& cb_fn) {                  //
                cy_sess_cntx.res_rx_cb_fn = std::forward<decltype(cb_fn)>(cb_fn);
            }));
        EXPECT_CALL(cy_transport_mock_, makeResponseRxSession(ResponseRxParamsEq(rx_params)))  //
            .WillOnce(Invoke([&](const auto&) {                                                //
                return libcyphal::detail::makeUniquePtr<CyUniquePtrResRxSpec>(mr_, cy_sess_cntx.res_rx_mock);
            }));
        EXPECT_CALL(cy_sess_cntx.res_rx_mock, deinit()).Times(1);
    }

    // NOLINTBEGIN
    ocvsmd::TrackingMemoryResource                  mr_;
    ocvsmd::VirtualTimeScheduler                    scheduler_{};
    StrictMock<libcyphal::transport::TransportMock> cy_transport_mock_;
    StrictMock<ipc::ServerRouterMock>               ipc_router_mock_{mr_};
    const std::string                               svc_name_{Spec::svc_full_name()};
    const ipc::detail::ServiceDesc svc_desc_{ipc::AnyChannel::getServiceDesc<Spec::Request>(svc_name_)};
    // NOLINTEND

};  // TestRawRpcClientService

// MARK: - Tests:

TEST_F(TestRawRpcClientService, registerWithContext)
{
    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_THAT(ipc_router_mock_.getChannelFactory(svc_desc_), IsNull());

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(svc_name_)).WillOnce(Return());
    relay::RawRpcClientService::registerWithContext(svc_context);

    EXPECT_THAT(ipc_router_mock_.getChannelFactory(svc_desc_), NotNull());
}

TEST_F(TestRawRpcClientService, request_config)
{
    using libcyphal::transport::TransferTxMetadataEq;

    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(_)).WillOnce(Return());
    relay::RawRpcClientService::registerWithContext(svc_context);

    auto* const ch_factory = ipc_router_mock_.getChannelFactory(svc_desc_);
    ASSERT_THAT(ch_factory, NotNull());

    StrictMock<GatewayMock> gateway_mock;

    Spec::Request request{&mr_};
    auto&         create_req  = request.set_create();
    create_req.extent_size    = CyService::Response::_traits_::ExtentBytes;
    create_req.service_id     = CyService::Request::_traits_::FixedPortId;
    create_req.server_node_id = 123;

    std::array<cetl::byte, 3> test_raw_bytes{b(0x11), b(0x22), b(0x33)};

    const auto expected_empty = VariantWith<EmptyResponse>(_);

    CySessCntx cy_sess_cntx;

    scheduler_.scheduleAt(1s, [&](const auto&) {
        //
        // Emulate service 'create' request.
        expectCySvcSessions(cy_sess_cntx, 123);
        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_empty)))
            .WillOnce(Return(OptError{}));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::make_shared<GatewayMock::Wrapper>(gateway_mock), payload);
            return OptError{};
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(2s, [&](const auto&) {
        //
        // Emulate service 'config' request.
        auto& config = request.set_config();
        config.priority.push_back(static_cast<std::uint8_t>(CyPriority::High));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{1, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(3s, [&](const auto&) {
        //
        // Emulate service 'empty config' request - should keep previously set 'High' priority.
        auto& config = request.set_config();
        config.priority.clear();
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{2, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(4s, [&](const auto&) {
        //
        // Emulate service 'send' request with empty payload.
        auto& send               = request.set_send();
        send.request_timeout_us  = 345;
        send.response_timeout_us = 741;
        send.payload_size        = 0;
        EXPECT_CALL(cy_sess_cntx.req_tx_mock, send(TransferTxMetadataEq({{0, CyPriority::High}, now() + 345us}), _))
            .WillOnce(Return(cetl::nullopt));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{3, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(4s + 740us, [&](const auto&) {
        //
        // Emulate that node 123 has replied with an empty raw message.
        RawMsgResponse raw_msg{&mr_};
        raw_msg.priority       = 4;
        raw_msg.remote_node_id = 123;
        EXPECT_CALL(gateway_mock,
                    send(_, io::PayloadVariantWith<Spec::Response>(mr_, VariantWith<RawMsgResponse>(raw_msg))))
            .WillOnce(Return(OptError{}));
        //
        CySvcRxTransfer transfer{{{{0, libcyphal::transport::Priority::Nominal}, now()}, 123}, {}};
        cy_sess_cntx.res_rx_cb_fn({transfer});
    });
    scheduler_.scheduleAt(5s, [&](const auto&) {
        //
        // Emulate service 'send' request with non-empty (3-bytes) payload.
        auto& send               = request.set_send();
        send.request_timeout_us  = 345;
        send.response_timeout_us = 741;
        send.payload_size        = test_raw_bytes.size();
        EXPECT_CALL(cy_sess_cntx.req_tx_mock, send(TransferTxMetadataEq({{1, CyPriority::High}, now() + 345us}), _))
            .WillOnce(Return(cetl::nullopt));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{4, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(5s + 740us, [&](const auto&) {
        //
        // Emulate that node 123 has replied with non-empty (3-bytes) payload.
        RawMsgResponse raw_msg{&mr_};
        raw_msg.priority       = 5;
        raw_msg.remote_node_id = 123;
        raw_msg.payload_size   = test_raw_bytes.size();
        EXPECT_CALL(gateway_mock,
                    send(_, io::PayloadVariantWith<Spec::Response>(mr_, VariantWith<RawMsgResponse>(raw_msg))))
            .WillOnce(Return(OptError{}));
        //
        NiceMock<CyScatteredBufferStorageMock> storage_mock;
        EXPECT_CALL(storage_mock, size()).WillRepeatedly(Return(3));
        EXPECT_CALL(storage_mock, forEachFragment(_)).WillOnce(Invoke([&](auto& visitor) {
            //
            visitor.onNext(test_raw_bytes);
        }));
        CySvcRxTransfer transfer{{{{1, libcyphal::transport::Priority::Low}, now()}, 123},
                                 CyScatteredBuffer{CyScatteredBufferStorageMock::Wrapper{&storage_mock}}};
        cy_sess_cntx.res_rx_cb_fn({transfer});
    });
    scheduler_.scheduleAt(9s, [&](const auto&) {
        //
        EXPECT_CALL(gateway_mock, complete(OptError{}, false)).WillOnce(Return(OptError{}));
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
        gateway_mock.event_handler_(GatewayEvent::Completed{OptError{}, false});
    });
    scheduler_.scheduleAt(9s + 1ms, [&](const auto&) {
        //
        testing::Mock::VerifyAndClearExpectations(&gateway_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_cntx.req_tx_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_cntx.res_rx_mock);
    });
    scheduler_.spinFor(10s);
}

TEST_F(TestRawRpcClientService, request_failure_on_cy_client)
{
    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(_)).WillOnce(Return());
    relay::RawRpcClientService::registerWithContext(svc_context);

    auto* const ch_factory = ipc_router_mock_.getChannelFactory(svc_desc_);
    ASSERT_THAT(ch_factory, NotNull());

    StrictMock<GatewayMock> gateway_mock;

    Spec::Request request{&mr_};
    auto&         create_req  = request.set_create();
    create_req.extent_size    = CyService::Response::_traits_::ExtentBytes;
    create_req.service_id     = CyService::Request::_traits_::FixedPortId;
    create_req.server_node_id = 123;

    const auto expected_empty = VariantWith<EmptyResponse>(_);

    CySessCntx cy_sess_cntx;

    scheduler_.scheduleAt(1s, [&](const auto&) {
        //
        // Emulate service 'create' request, which fails on Cyphal RPC client creation.
        EXPECT_CALL(cy_transport_mock_, makeRequestTxSession(_))  //
            .WillOnce(Invoke([&](const auto&) { return nullptr; }));
        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        EXPECT_CALL(gateway_mock, complete(OptError{Error::Code::OutOfMemory}, false))  //
            .WillOnce(Return(OptError{}));
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::make_shared<GatewayMock::Wrapper>(gateway_mock), payload);
            return OptError{};
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(2s, [&](const auto&) {
        //
        // Emulate service 'create' request, which fails on reply.
        expectCySvcSessions(cy_sess_cntx, 123);
        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_empty)))
            .WillOnce(Return(Error{Error::Code::Disconnected}));
        EXPECT_CALL(gateway_mock, complete(OptError{Error::Code::Disconnected}, false))  //
            .WillOnce(Return(OptError{}));
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::make_shared<GatewayMock::Wrapper>(gateway_mock), payload);
            return OptError{};
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(9s, [&](const auto&) {
        //
        testing::Mock::VerifyAndClearExpectations(&gateway_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_cntx.req_tx_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_cntx.res_rx_mock);
    });
    scheduler_.spinFor(10s);
}

TEST_F(TestRawRpcClientService, request_failure_on_cy_request)
{
    using libcyphal::transport::TransferTxMetadataEq;

    CyPresentation   cy_presentation{mr_, scheduler_, cy_transport_mock_};
    const ScvContext svc_context{mr_, scheduler_, ipc_router_mock_, cy_presentation};

    EXPECT_CALL(ipc_router_mock_, registerChannelFactoryByName(_)).WillOnce(Return());
    relay::RawRpcClientService::registerWithContext(svc_context);

    auto* const ch_factory = ipc_router_mock_.getChannelFactory(svc_desc_);
    ASSERT_THAT(ch_factory, NotNull());

    StrictMock<GatewayMock> gateway_mock;

    Spec::Request request{&mr_};
    auto&         create_req  = request.set_create();
    create_req.extent_size    = CyService::Response::_traits_::ExtentBytes;
    create_req.service_id     = CyService::Request::_traits_::FixedPortId;
    create_req.server_node_id = 123;

    const auto expected_empty = VariantWith<EmptyResponse>(_);

    ErrorResponse expected_error{&mr_};
    optErrorToDsdlError(Error{Error::Code::InvalidArgument}, expected_error);
    const auto expected_error_matcher = VariantWith<ErrorResponse>(expected_error);

    CySessCntx cy_sess_cntx;

    scheduler_.scheduleAt(1s, [&](const auto&) {
        //
        // Emulate service 'create' request.
        expectCySvcSessions(cy_sess_cntx, 123);
        EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_empty)))
            .WillOnce(Return(OptError{}));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            (*ch_factory)(std::make_shared<GatewayMock::Wrapper>(gateway_mock), payload);
            return OptError{};
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(2s, [&](const auto&) {
        //
        // Emulate service 'send' request with empty payload, which fails on Cyphal RPC client TX transfer.
        auto& send               = request.set_send();
        send.request_timeout_us  = 345;
        send.response_timeout_us = 741;
        send.payload_size        = 0;
        EXPECT_CALL(cy_sess_cntx.req_tx_mock, send(_, _)).WillOnce(Return(libcyphal::ArgumentError{}));
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_error_matcher)))
            .WillOnce(Return(OptError{}));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{1, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(3s, [&](const auto&) {
        //
        // Emulate service 'send' request with empty payload, which fails on Cyphal RPC client RX timeout.
        auto& send               = request.set_send();
        send.request_timeout_us  = 345;
        send.response_timeout_us = 741;
        send.payload_size        = 0;
        EXPECT_CALL(cy_sess_cntx.req_tx_mock, send(_, _)).WillOnce(Return(cetl::nullopt));
        const auto result = tryPerformOnSerialized(request, [&](const auto payload) {
            //
            return gateway_mock.event_handler_(GatewayEvent::Message{2, payload});
        });
        EXPECT_THAT(result, OptError{});
    });
    scheduler_.scheduleAt(3s + 741us, [&](const auto&) {
        //
        ErrorResponse expected_timeout_error{&mr_};
        optErrorToDsdlError(Error{Error::Code::TimedOut}, expected_timeout_error);
        const auto expected_timeout_matcher = VariantWith<ErrorResponse>(expected_timeout_error);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Response>(mr_, expected_timeout_matcher)))
            .WillOnce(Return(OptError{}));
    });
    scheduler_.scheduleAt(9s, [&](const auto&) {
        //
        EXPECT_CALL(gateway_mock, complete(OptError{}, false)).WillOnce(Return(OptError{}));
        EXPECT_CALL(gateway_mock, deinit()).Times(1);
        gateway_mock.event_handler_(GatewayEvent::Completed{OptError{}, false});
    });
    scheduler_.scheduleAt(9s + 1ms, [&](const auto&) {
        //
        testing::Mock::VerifyAndClearExpectations(&gateway_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_cntx.req_tx_mock);
        testing::Mock::VerifyAndClearExpectations(&cy_sess_cntx.res_rx_mock);
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
static void PrintTo(const RawRpcClientReceive_0_1& raw_msg, std::ostream* os)  // NOLINT
{
    *os << "relay::RawRpcClientReceive_0_1{priority=" << static_cast<int>(raw_msg.priority)
        << ", node_id=" << raw_msg.remote_node_id << ", payload_size=" << raw_msg.payload_size << "}";
}
static bool operator==(const RawRpcClientReceive_0_1& lhs, const RawRpcClientReceive_0_1& rhs)  // NOLINT
{
    return (lhs.priority == rhs.priority) && (lhs.remote_node_id == rhs.remote_node_id) &&
           (lhs.payload_size == rhs.payload_size);
}
}  // namespace relay
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd
