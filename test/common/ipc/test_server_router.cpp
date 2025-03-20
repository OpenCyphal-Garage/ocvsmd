//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "ipc/server_router.hpp"

#include "common_helpers.hpp"
#include "ipc/channel.hpp"
#include "ipc/ipc_types.hpp"
#include "ipc/pipe/server_pipe.hpp"
#include "ipc_gtest_helpers.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "pipe/server_pipe_mock.hpp"
#include "tracking_memory_resource.hpp"

#include "ocvsmd/common/ipc/Route_0_2.hpp"
#include "ocvsmd/common/svc/node/ExecCmd_0_2.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace
{

using namespace ocvsmd::common::ipc;  // NOLINT This our main concern here in the unit tests.
using ocvsmd::sdk::Error;
using ocvsmd::sdk::OptError;

using testing::_;
using testing::IsTrue;
using testing::Return;
using testing::IsEmpty;
using testing::IsFalse;
using testing::NotNull;
using testing::Optional;
using testing::StrictMock;
using testing::VariantWith;
using testing::MockFunction;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, bugprone-unchecked-optional-access)

class TestServerRouter : public testing::Test
{
protected:
    void SetUp() override
    {
        cetl::pmr::set_default_resource(&mr_);
    }

    void TearDown() override
    {
        EXPECT_THAT(mr_.allocations, IsEmpty());
        EXPECT_THAT(mr_.total_allocated_bytes, mr_.total_deallocated_bytes);
    }

    void emulateRouteConnect(const pipe::ServerPipe::ClientId client_id,
                             pipe::ServerPipeMock&            server_pipe_mock,
                             const std::uint8_t               ver_major = VERSION_MAJOR,  // NOLINT
                             const std::uint8_t               ver_minor = VERSION_MINOR,
                             const OptError                   opt_error = {})
    {
        using ocvsmd::common::tryPerformOnSerialized;

        server_pipe_mock.event_handler_(pipe::ServerPipe::Event::Connected{client_id});

        Route_0_2 route{&mr_};
        auto&     rt_conn     = route.set_connect();
        rt_conn.version.major = ver_major;
        rt_conn.version.minor = ver_minor;
        optErrorToDsdlError(opt_error, rt_conn._error);
        //
        EXPECT_CALL(server_pipe_mock, send(client_id, PayloadOfRouteConnect(mr_)))  //
            .WillOnce(Return(OptError{}));
        const auto result = tryPerformOnSerialized(route, [&](const auto payload) {
            //
            return server_pipe_mock.event_handler_(pipe::ServerPipe::Event::Message{client_id, payload});
        });
        EXPECT_THAT(result, OptError{});
    }

    template <typename Msg>
    void emulateRouteChannelMsg(const pipe::ServerPipe::ClientId client_id,
                                pipe::ServerPipeMock&            server_pipe_mock,
                                const std::uint64_t              tag,
                                const Msg&                       msg,
                                std::uint64_t&                   seq,
                                const cetl::string_view          service_name = "")
    {
        using ocvsmd::common::tryPerformOnSerialized;

        Route_0_2 route{&mr_};
        auto&     channel_msg  = route.set_channel_msg();
        channel_msg.tag        = tag;
        channel_msg.sequence   = seq++;
        channel_msg.service_id = AnyChannel::getServiceDesc<Msg>(service_name).id;

        const auto result = tryPerformOnSerialized(route, [&](const auto prefix) {
            //
            return tryPerformOnSerialized(msg, [&](const auto suffix) {
                //
                std::vector<std::uint8_t> buffer;
                std::copy(prefix.begin(), prefix.end(), std::back_inserter(buffer));
                std::copy(suffix.begin(), suffix.end(), std::back_inserter(buffer));
                const Payload payload{buffer.data(), buffer.size()};
                return server_pipe_mock.event_handler_(pipe::ServerPipe::Event::Message{client_id, payload});
            });
        });
        EXPECT_THAT(result, OptError{});
    }

    void emulateRouteChannelEnd(const pipe::ServerPipe::ClientId client_id,
                                pipe::ServerPipeMock&            server_pipe_mock,
                                const std::uint64_t              tag,
                                const OptError                   opt_error  = {},
                                const bool                       keep_alive = false)
    {
        using ocvsmd::common::tryPerformOnSerialized;

        Route_0_2 route{&mr_};
        auto&     channel_end  = route.set_channel_end();
        channel_end.tag        = tag;
        channel_end.keep_alive = keep_alive;
        optErrorToDsdlError(opt_error, channel_end._error);

        const auto result = tryPerformOnSerialized(route, [&](const auto payload) {
            //
            return server_pipe_mock.event_handler_(pipe::ServerPipe::Event::Message{client_id, payload});
        });
        EXPECT_THAT(result, OptError{});
    }

    // MARK: Data members:

    // NOLINTBEGIN
    ocvsmd::TrackingMemoryResource mr_;
    // NOLINTEND
};

// MARK: - Tests:

TEST_F(TestServerRouter, make)
{
    StrictMock<pipe::ServerPipeMock> server_pipe_mock;
    EXPECT_CALL(server_pipe_mock, deinit()).Times(1);

    const auto server_router = ServerRouter::make(  //
        mr_,
        std::make_unique<pipe::ServerPipeMock::Wrapper>(server_pipe_mock));
    ASSERT_THAT(server_router, NotNull());
    EXPECT_THAT(server_pipe_mock.event_handler_, IsFalse());
}

TEST_F(TestServerRouter, start)
{
    StrictMock<pipe::ServerPipeMock> server_pipe_mock;
    EXPECT_CALL(server_pipe_mock, deinit()).Times(1);

    const auto server_router = ServerRouter::make(  //
        mr_,
        std::make_unique<pipe::ServerPipeMock::Wrapper>(server_pipe_mock));
    ASSERT_THAT(server_router, NotNull());
    EXPECT_THAT(server_pipe_mock.event_handler_, IsFalse());

    EXPECT_CALL(server_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(server_router->start(), OptError{});
    EXPECT_THAT(server_pipe_mock.event_handler_, IsTrue());
}

TEST_F(TestServerRouter, registerChannel)
{
    using Msg     = ocvsmd::common::svc::node::ExecCmd::Request_0_2;
    using Channel = Channel<Msg, Msg>;

    StrictMock<pipe::ServerPipeMock> server_pipe_mock;
    EXPECT_CALL(server_pipe_mock, deinit()).Times(1);

    const auto server_router = ServerRouter::make(  //
        mr_,
        std::make_unique<pipe::ServerPipeMock::Wrapper>(server_pipe_mock));
    ASSERT_THAT(server_router, NotNull());
    EXPECT_THAT(server_pipe_mock.event_handler_, IsFalse());

    EXPECT_CALL(server_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(server_router->start(), OptError{});
    EXPECT_THAT(server_pipe_mock.event_handler_, IsTrue());

    server_router->registerChannel<Channel>("", [](auto&&, const auto&) {});
}

TEST_F(TestServerRouter, channel_send)
{
    using Msg     = ocvsmd::common::svc::node::ExecCmd::Request_0_2;
    using Channel = Channel<Msg, Msg>;

    StrictMock<pipe::ServerPipeMock> server_pipe_mock;
    EXPECT_CALL(server_pipe_mock, deinit()).Times(1);

    const auto server_router = ServerRouter::make(  //
        mr_,
        std::make_unique<pipe::ServerPipeMock::Wrapper>(server_pipe_mock));
    ASSERT_THAT(server_router, NotNull());
    EXPECT_THAT(server_pipe_mock.event_handler_, IsFalse());

    EXPECT_CALL(server_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(server_router->start(), OptError{});
    EXPECT_THAT(server_pipe_mock.event_handler_, IsTrue());

    StrictMock<MockFunction<void(const Channel::EventVar&, const Payload)>> ch_event_mock;

    cetl::optional<Channel> maybe_channel;
    server_router->registerChannel<Channel>("", [&](Channel&& ch, const auto& input) {
        //
        ch.subscribe(ch_event_mock.AsStdFunction());
        maybe_channel = std::move(ch);
        ch_event_mock.Call(input, {});
    });
    EXPECT_THAT(maybe_channel.has_value(), IsFalse());

    // Emulate that client #42 is connected.
    //
    constexpr std::uint64_t cl_id = 42;
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Connected>(_), _)).Times(1);
    emulateRouteConnect(cl_id, server_pipe_mock);

    // Emulate that client posted initial `RouteChannelMsg` on 42/7 client/tag pair.
    //
    constexpr std::uint64_t tag = 7;
    std::uint64_t           seq = 0;
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Input>(_), _)).Times(1);
    emulateRouteChannelMsg(cl_id, server_pipe_mock, tag, Channel::Input{&mr_}, seq);
    ASSERT_THAT(maybe_channel.has_value(), IsTrue());
    EXPECT_CALL(server_pipe_mock, send(cl_id, PayloadOfRouteChannelEnd(mr_, tag, OptError{})))
        .WillOnce(Return(OptError{}));

    // Emulate that client posted one more `RouteChannelMsg` on the same 42/7 client/tag pair.
    //
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Input>(_), _)).Times(1);
    emulateRouteChannelMsg(cl_id, server_pipe_mock, tag, Channel::Input{&mr_}, seq);

    // Emulate that client posted final `RouteChannelEnd` on the same 42/7 client/tag pair.
    //
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Completed>(_), _)).Times(1);
    emulateRouteChannelEnd(cl_id, server_pipe_mock, tag, OptError{}, true);

    seq = 0;
    const Channel::Output msg{&mr_};
    EXPECT_CALL(server_pipe_mock, send(cl_id, PayloadOfRouteChannelMsg(msg, mr_, tag, seq++)))  //
        .WillOnce(Return(OptError{}));
    EXPECT_THAT(maybe_channel->send(msg, {}), OptError{});

    EXPECT_CALL(server_pipe_mock, send(cl_id, PayloadOfRouteChannelMsg(msg, mr_, tag, seq++)))  //
        .WillOnce(Return(OptError{}));
    EXPECT_THAT(maybe_channel->send(msg, {}), OptError{});
}

TEST_F(TestServerRouter, channel_send_after_end)
{
    using Msg     = ocvsmd::common::svc::node::ExecCmd::Request_0_2;
    using Channel = Channel<Msg, Msg>;

    StrictMock<pipe::ServerPipeMock> server_pipe_mock;
    EXPECT_CALL(server_pipe_mock, deinit()).Times(1);

    const auto server_router = ServerRouter::make(  //
        mr_,
        std::make_unique<pipe::ServerPipeMock::Wrapper>(server_pipe_mock));
    ASSERT_THAT(server_router, NotNull());
    EXPECT_THAT(server_pipe_mock.event_handler_, IsFalse());

    EXPECT_CALL(server_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(server_router->start(), OptError{});
    EXPECT_THAT(server_pipe_mock.event_handler_, IsTrue());

    StrictMock<MockFunction<void(const Channel::EventVar&, const Payload)>> ch_event_mock;

    cetl::optional<Channel> maybe_channel;
    server_router->registerChannel<Channel>("", [&](Channel&& ch, const auto& input) {
        //
        ch.subscribe(ch_event_mock.AsStdFunction());
        maybe_channel = std::move(ch);
        ch_event_mock.Call(input, {});
    });
    EXPECT_THAT(maybe_channel.has_value(), IsFalse());

    // Emulate that client #43 is connected.
    //
    constexpr std::uint64_t cl_id = 43;
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Connected>(_), _)).Times(1);
    emulateRouteConnect(cl_id, server_pipe_mock);

    // Emulate that client posted initial `RouteChannelMsg` on 43/8 client/tag pair.
    //
    constexpr std::uint64_t tag = 8;
    std::uint64_t           seq = 0;
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Input>(_), _)).Times(1);
    emulateRouteChannelMsg(cl_id, server_pipe_mock, tag, Channel::Input{&mr_}, seq);
    ASSERT_THAT(maybe_channel.has_value(), IsTrue());

    // Emulate that client posted final `RouteChannelEnd(keep-alive)` on the same 43/8 client/tag pair.
    //
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Completed>(_), _)).Times(1);
    emulateRouteChannelEnd(cl_id, server_pipe_mock, tag, OptError{}, true);

    seq = 0;
    const Channel::Output msg{&mr_};
    EXPECT_CALL(server_pipe_mock, send(cl_id, PayloadOfRouteChannelMsg(msg, mr_, tag, seq++)))  //
        .WillOnce(Return(OptError{}));
    EXPECT_THAT(maybe_channel->send(msg, {}), OptError{});

    EXPECT_CALL(server_pipe_mock, send(cl_id, PayloadOfRouteChannelMsg(msg, mr_, tag, seq++)))  //
        .WillOnce(Return(OptError{}));
    EXPECT_THAT(maybe_channel->send(msg, {}), OptError{});

    EXPECT_CALL(server_pipe_mock, send(cl_id, PayloadOfRouteChannelEnd(mr_, tag, OptError{}, true)))
        .WillOnce(Return(OptError{}));
    EXPECT_THAT(maybe_channel->complete(OptError{}, true), OptError{});

    // Emulate that client posted terminal `RouteChannelEnd` on the same 43/8 client/tag pair.
    //
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Completed>(_), _)).Times(1);
    emulateRouteChannelEnd(cl_id, server_pipe_mock, tag, OptError{});
    //
    EXPECT_THAT(maybe_channel->send(msg, {}), Optional(Error{Error::Code::Shutdown}));
    EXPECT_THAT(maybe_channel->complete(), Optional(Error{Error::Code::Shutdown}));

    // Emulate that the whole client has been disconnected.
    //
    server_pipe_mock.event_handler_(pipe::ServerPipe::Event::Disconnected{cl_id});
    //
    EXPECT_THAT(maybe_channel->send(msg, {}), Optional(Error{Error::Code::NotConnected}));
    EXPECT_THAT(maybe_channel->complete(), Optional(Error{Error::Code::NotConnected}));
}

TEST_F(TestServerRouter, channel_disconnected)
{
    using Msg     = ocvsmd::common::svc::node::ExecCmd::Request_0_2;
    using Channel = Channel<Msg, Msg>;

    StrictMock<pipe::ServerPipeMock> server_pipe_mock;
    EXPECT_CALL(server_pipe_mock, deinit()).Times(1);

    const auto server_router = ServerRouter::make(  //
        mr_,
        std::make_unique<pipe::ServerPipeMock::Wrapper>(server_pipe_mock));
    ASSERT_THAT(server_router, NotNull());
    EXPECT_THAT(server_pipe_mock.event_handler_, IsFalse());

    EXPECT_CALL(server_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(server_router->start(), OptError{});
    EXPECT_THAT(server_pipe_mock.event_handler_, IsTrue());

    StrictMock<MockFunction<void(const Channel::EventVar&, const Payload)>> ch_event_mock;

    cetl::optional<Channel> maybe_channel;
    server_router->registerChannel<Channel>("", [&](Channel&& ch, const auto& input) {
        //
        ch.subscribe(ch_event_mock.AsStdFunction());
        maybe_channel = std::move(ch);
        ch_event_mock.Call(input, {});
    });
    EXPECT_THAT(maybe_channel.has_value(), IsFalse());

    // Emulate that client #43 is connected.
    //
    constexpr std::uint64_t cl_id = 43;
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Connected>(_), _)).Times(1);
    emulateRouteConnect(cl_id, server_pipe_mock);

    // Emulate that client posted initial `RouteChannelMsg` on 43/8 client/tag pair.
    //
    constexpr std::uint64_t tag = 8;
    std::uint64_t           seq = 0;
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Input>(_), _)).Times(1);
    emulateRouteChannelMsg(cl_id, server_pipe_mock, tag, Channel::Input{&mr_}, seq);
    ASSERT_THAT(maybe_channel.has_value(), IsTrue());

    // Emulate that client posted terminal `RouteChannelEnd` on unknown tag.
    //
    emulateRouteChannelEnd(cl_id, server_pipe_mock, tag + 1, OptError{});

    // Emulate that unknown client posted terminal `RouteChannelEnd`.
    //
    emulateRouteChannelEnd(cl_id + 1, server_pipe_mock, tag, OptError{});

    // Emulate that the whole client has been disconnected.
    //
    EXPECT_CALL(ch_event_mock, Call(VariantWith<Channel::Completed>(_), _)).Times(1);
    server_pipe_mock.event_handler_(pipe::ServerPipe::Event::Disconnected{cl_id});
}

TEST_F(TestServerRouter, channel_unsolicited)
{
    using Msg           = ocvsmd::common::svc::node::ExecCmd::Request_0_2;
    using Channel       = Channel<Msg, Msg>;
    using UnexpectedMsg = ocvsmd::common::svc::node::ExecCmd::Response_0_2;

    StrictMock<pipe::ServerPipeMock> server_pipe_mock;
    EXPECT_CALL(server_pipe_mock, deinit()).Times(1);

    const auto server_router = ServerRouter::make(  //
        mr_,
        std::make_unique<pipe::ServerPipeMock::Wrapper>(server_pipe_mock));
    ASSERT_THAT(server_router, NotNull());
    EXPECT_THAT(server_pipe_mock.event_handler_, IsFalse());

    EXPECT_CALL(server_pipe_mock, start(_)).Times(1);
    EXPECT_THAT(server_router->start(), OptError{});
    EXPECT_THAT(server_pipe_mock.event_handler_, IsTrue());

    StrictMock<MockFunction<void(const Channel::EventVar&, const Payload)>> ch_event_mock;

    cetl::optional<Channel> maybe_channel;
    server_router->registerChannel<Channel>("", [&](Channel&& ch, const auto& input) {
        //
        ch.subscribe(ch_event_mock.AsStdFunction());
        maybe_channel = std::move(ch);
        ch_event_mock.Call(input, {});
    });
    EXPECT_THAT(maybe_channel.has_value(), IsFalse());

    // Emulate that client #42 is connected.
    //
    constexpr std::uint64_t cl_id = 43;
    emulateRouteConnect(cl_id, server_pipe_mock);

    // Emulate that client posted initial `RouteChannelMsg` on 43/8 client/tag pair,
    // but with wrong/unexpected sequence number != 0.
    //
    constexpr std::uint64_t tag = 8;
    std::uint64_t           seq = 1;
    emulateRouteChannelMsg(cl_id, server_pipe_mock, tag, Channel::Input{&mr_}, seq);
    ASSERT_THAT(maybe_channel.has_value(), IsFalse());

    // Emulate that client posted initial `RouteChannelMsg` on 43/8 client/tag pair,
    // but with unknown service id.
    //
    seq = 0;
    emulateRouteChannelMsg(cl_id, server_pipe_mock, tag, UnexpectedMsg{&mr_}, seq);
    ASSERT_THAT(maybe_channel.has_value(), IsFalse());

    // Emulate that unknown/unexpected client posted initial `RouteChannelMsg`.
    //
    seq = 0;
    emulateRouteChannelMsg(cl_id - 1, server_pipe_mock, tag, Channel::Input{&mr_}, seq);
    ASSERT_THAT(maybe_channel.has_value(), IsFalse());
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, bugprone-unchecked-optional-access)

}  // namespace
