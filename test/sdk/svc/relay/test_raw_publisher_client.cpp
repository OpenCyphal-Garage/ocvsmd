//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "svc/relay/raw_publisher_client.hpp"

#include "common/io/io_gtest_helpers.hpp"
#include "common/ipc/client_router_mock.hpp"
#include "svc/client_helpers.hpp"
#include "tracking_memory_resource.hpp"

#include <uavcan/node/Version_1_0.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace
{

using namespace ocvsmd::common;    // NOLINT This our main concern here in the unit tests.
using namespace ocvsmd::sdk::svc;  // NOLINT This our main concern here in the unit tests.
using ocvsmd::sdk::Error;
using ocvsmd::sdk::OptError;

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::IsEmpty;
using testing::NotNull;
using testing::Optional;
using testing::StrictMock;
using testing::ElementsAre;
using testing::VariantWith;

// https://github.com/llvm/llvm-project/issues/53444
// NOLINTBEGIN(misc-unused-using-decls, misc-include-cleaner)
using std::literals::chrono_literals::operator""us;
// NOLINTEND(misc-unused-using-decls, misc-include-cleaner)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

class TestRawPublisherClient : public testing::Test
{
protected:
    using Spec           = svc::relay::RawPublisherSpec;
    using GatewayMock    = ipc::detail::GatewayMock;
    using GatewayEvent   = ipc::detail::Gateway::Event;
    using Publisher      = ocvsmd::sdk::Publisher;
    using ConfigRequest  = Spec::Request::_traits_::TypeOf::config;
    using CreateRequest  = Spec::Request::_traits_::TypeOf::create;
    using PublishRequest = Spec::Request::_traits_::TypeOf::publish;
    using CyphalPriority = ocvsmd::sdk::CyphalPriority;

    void SetUp() override
    {
        cetl::pmr::set_default_resource(&mr_);
    }

    void TearDown() override
    {
        EXPECT_THAT(mr_.allocations, IsEmpty());
        EXPECT_THAT(mr_.total_allocated_bytes, mr_.total_deallocated_bytes);
    }

    // NOLINTBEGIN
    ocvsmd::TrackingMemoryResource    mr_;
    StrictMock<ipc::ClientRouterMock> ipc_router_mock_{mr_};
    // NOLINTEND

};  // TestRawPublisherClient

// MARK: - Tests:

TEST_F(TestRawPublisherClient, make)
{
    StrictMock<GatewayMock> gateway_mock;

    const Spec::Request request{&mr_};
    const ClientContext context{mr_, ipc_router_mock_};
    EXPECT_CALL(ipc_router_mock_, makeGateway()).WillOnce(Invoke([&] {
        //
        return std::make_shared<GatewayMock::Wrapper>(gateway_mock);
    }));
    auto client = relay::RawPublisherClient::make(context, request);
    EXPECT_CALL(gateway_mock, deinit()).Times(1);
}

TEST_F(TestRawPublisherClient, submit_create_config_publish)
{
    StrictMock<GatewayMock> gateway_mock;

    EXPECT_CALL(ipc_router_mock_, makeGateway()).WillOnce(Invoke([&] {
        //
        return std::make_shared<GatewayMock::Wrapper>(gateway_mock);
    }));

    Spec::Request  request{&mr_};
    Spec::Response response{&mr_};

    auto& create_req      = request.set_create();
    create_req.subject_id = 123;
    const ClientContext context{mr_, ipc_router_mock_};
    auto                svc_client_ = relay::RawPublisherClient::make(context, request);

    Publisher::Ptr publisher;
    EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
    svc_client_->submit([&](auto result) {
        //
        ASSERT_THAT(result, VariantWith<Publisher::Ptr>(testing::NotNull()));
        publisher = cetl::get<Publisher::Ptr>(std::move(result));
    });

    // Emulate that we've got connection - it should initiate IPC request.
    {
        const auto expected_create = VariantWith<CreateRequest>(create_req);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Request>(mr_, expected_create)))
            .WillOnce(Return(OptError{}));
        gateway_mock.event_handler_(GatewayEvent::Connected{});
    }

    // Emulate that IPC server replied with empty success - it should trigger receiving of the publisher.
    {
        response.set_empty();
        const auto result = tryPerformOnSerialized(response, [&](const auto payload) {
            //
            EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
            return gateway_mock.event_handler_(GatewayEvent::Message{0, payload});
        });
        EXPECT_THAT(result, OptError{});
        EXPECT_THAT(publisher, NotNull());
    }

    // Verify `setPriority` configuration
    {
        auto& config_req = request.set_config();
        config_req.priority.push_back(static_cast<std::uint8_t>(CyphalPriority::High));
        const auto expected_config = VariantWith<ConfigRequest>(config_req);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Request>(mr_, expected_config)))
            .WillOnce(Return(OptError{}));

        publisher->setPriority(CyphalPriority::High);
    }

    // Verify `publish`
    {
        const uavcan::node::Version_1_0 test_msg{2, 3, &mr_};

        auto pub_sender = publisher->publish(test_msg, 741us);

        auto& pub_req           = request.set_publish();
        pub_req.timeout_us      = 741;
        pub_req.payload_size    = 2;
        const auto expected_pub = VariantWith<PublishRequest>(pub_req);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Request>(mr_, expected_pub)))
            .WillOnce(Return(OptError{}));

        std::vector<OptError> pub_responses;
        pub_sender->submit([&](auto result) {
            //
            pub_responses.push_back(std::move(result));
        });

        // Emulate that IPC server replied with success.
        {
            auto& pub_res      = response.set_publish_error();
            pub_res.error_code = 0;
            pub_res.raw_errno  = 0;
            const auto result  = tryPerformOnSerialized(response, [&](const auto payload) {
                //
                return gateway_mock.event_handler_(GatewayEvent::Message{0, payload});
            });
            EXPECT_THAT(result, OptError{});
            EXPECT_THAT(pub_responses, ElementsAre(OptError{}));
        }
    }

    EXPECT_CALL(gateway_mock, deinit()).Times(1);
    svc_client_.reset();
}

TEST_F(TestRawPublisherClient, config_publish_after_completion_error)
{
    StrictMock<GatewayMock> gateway_mock;

    EXPECT_CALL(ipc_router_mock_, makeGateway()).WillOnce(Invoke([&] {
        //
        return std::make_shared<GatewayMock::Wrapper>(gateway_mock);
    }));

    Spec::Request  request{&mr_};
    Spec::Response response{&mr_};

    auto& create_req      = request.set_create();
    create_req.subject_id = 123;
    const ClientContext context{mr_, ipc_router_mock_};
    auto                svc_client_ = relay::RawPublisherClient::make(context, request);

    Publisher::Ptr publisher;
    EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
    svc_client_->submit([&](auto result) {
        //
        ASSERT_THAT(result, VariantWith<Publisher::Ptr>(testing::NotNull()));
        publisher = cetl::get<Publisher::Ptr>(std::move(result));
    });

    // Emulate that we've got connection - it should initiate IPC request.
    {
        const auto expected_create = VariantWith<CreateRequest>(create_req);
        EXPECT_CALL(gateway_mock, send(_, io::PayloadVariantWith<Spec::Request>(mr_, expected_create)))
            .WillOnce(Return(OptError{}));
        gateway_mock.event_handler_(GatewayEvent::Connected{});
    }

    // Emulate that IPC server replied with empty success - it should trigger receiving of the publisher.
    {
        response.set_empty();
        const auto result = tryPerformOnSerialized(response, [&](const auto payload) {
            //
            EXPECT_CALL(gateway_mock, subscribe(_)).Times(1);
            return gateway_mock.event_handler_(GatewayEvent::Message{0, payload});
        });
        EXPECT_THAT(result, OptError{});
        EXPECT_THAT(publisher, NotNull());
    }

    // Try to set priority after IPC server disconnected - should fail.
    const auto test_failure = Error{Error::Code::Disconnected};
    {
        gateway_mock.event_handler_(GatewayEvent::Completed{OptError{test_failure}, false});

        EXPECT_THAT(publisher->setPriority(CyphalPriority::Fast), Optional(test_failure));
    }
    // Try to publish after IPC server disconnected - should receive failure.
    {
        const uavcan::node::Version_1_0 test_msg{2, 3, &mr_};
        auto                            pub_sender = publisher->publish(test_msg, 741us);

        std::vector<OptError> pub_responses;
        pub_sender->submit([&](auto result) {
            //
            pub_responses.push_back(std::move(result));
        });
        EXPECT_THAT(pub_responses, ElementsAre(Optional(test_failure)));
    }

    EXPECT_CALL(gateway_mock, deinit()).Times(1);
    svc_client_.reset();
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
static void PrintTo(const RawPublisherConfig_0_1& request, std::ostream* os)  // NOLINT
{
    *os << "relay::RawPublisherConfig_0_1{priority="
        << (request.priority.empty() ? std::string("null") : std::to_string(request.priority.front())) << "}";
}
static void PrintTo(const RawPublisherCreate_0_1& request, std::ostream* os)  // NOLINT
{
    *os << "relay::RawPublisherCreate_0_1{subj_id=" << request.subject_id << "}";
}
static void PrintTo(const RawPublisherPublish_0_1& request, std::ostream* os)  // NOLINT
{
    *os << "relay::RawPublisherPublish_0_1{timeout_us=" << request.timeout_us
        << ", payload_size=" << request.payload_size << "}";
}
static bool operator==(const RawPublisherConfig_0_1& lhs, const RawPublisherConfig_0_1& rhs)  // NOLINT
{
    return lhs.priority == rhs.priority;
}
static bool operator==(const RawPublisherCreate_0_1& lhs, const RawPublisherCreate_0_1& rhs)  // NOLINT
{
    return lhs.subject_id == rhs.subject_id;
}
static bool operator==(const RawPublisherPublish_0_1& lhs, const RawPublisherPublish_0_1& rhs)  // NOLINT
{
    return (lhs.timeout_us == rhs.timeout_us) && (lhs.payload_size == rhs.payload_size);
}
}  // namespace relay
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd
