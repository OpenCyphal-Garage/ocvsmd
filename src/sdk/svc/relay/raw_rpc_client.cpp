//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "raw_rpc_client.hpp"

#include "common_helpers.hpp"
#include "io/socket_buffer.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/node_rpc_client.hpp"
#include "svc/client_helpers.hpp"

#include <ocvsmd/common/svc/relay/RawRpcClientReceive_0_1.hpp>
#include <uavcan/primitive/Empty_1_0.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/visit_helpers.hpp>

#include <memory>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace relay
{
namespace
{

class RawRpcClientImpl final : public RawRpcClient
{
public:
    RawRpcClientImpl(const ClientContext& context, Spec::Request request)
        : context_{context}
        , request_{std::move(request)}
        , channel_{context.ipc_router.makeChannel<Channel>(Spec::svc_full_name())}
    {
    }

    void submitImpl(std::function<void(Result&&)>&& receiver) override
    {
        receiver_ = std::move(receiver);

        channel_.subscribe([this](const auto& event_var, const auto) {
            //
            cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
        });
    }

private:
    using Channel = common::ipc::Channel<Spec::Response, Spec::Request>;

    class NodeRpcClientImpl final : public std::enable_shared_from_this<NodeRpcClientImpl>, public NodeRpcClient
    {
    public:
        NodeRpcClientImpl(const ClientContext& context, Channel&& channel)
            : context_{context}
            , channel_{std::move(channel)}
            , requested_{CallRequest{&context.memory}, {}}
        {
            channel_.subscribe([this](const auto& event_var, const auto payload) {
                //
                cetl::visit(                //
                    cetl::make_overloaded(  //
                        [this, payload](const Channel::Input& input) {
                            //
                            handleEvent(input, payload);
                        },
                        [this](const Channel::Completed& completed) {
                            //
                            handleEvent(completed);
                        },
                        [this](const Channel::Connected&) {}),
                    event_var);
            });
        }

        template <typename Receiver>
        void submit(Receiver&& receiver)
        {
            if (const auto error = completion_error_)
            {
                context_.logger->warn("NodeRpcClient::submit() Already completed with error (err={}).", *error);
                receiver(*error);
                return;
            }

            Spec::Request request{&context_.memory};
            auto&         call = request.set_call(requested_.call);
            call.payload_size  = requested_.payload.size;
            SocketBuffer sock_buff{{requested_.payload.data.get(), call.payload_size}};
            const auto   opt_send_error = channel_.send(request, sock_buff);

            // Raw request payload has been sent, so no need to keep it in memory.
            requested_.payload.size = 0;
            requested_.payload.data.reset();

            if (const auto error = opt_send_error)
            {
                context_.logger->warn("NodeRpcClient::submit() Failed to send 'call' request (err={}).", *error);
                receiver(*error);
                return;
            }

            receiver_ = std::forward<Receiver>(receiver);
        }

        // NodeRpcClient

        SenderOf<RawCall::Result>::Ptr rawCall(OwnedMutablePayload&&           raw_request_payload,
                                               const std::chrono::microseconds request_timeout,
                                               const std::chrono::microseconds response_timeout) override
        {
            requested_.payload                  = std::move(raw_request_payload);
            requested_.call.request_timeout_us  = std::max<std::uint64_t>(0, request_timeout.count());
            requested_.call.response_timeout_us = std::max<std::uint64_t>(0, response_timeout.count());

            return std::make_unique<AsSender<RawCall::Result, decltype(shared_from_this())>>(  //
                "NodeRpcClient::rawRequest",
                shared_from_this(),
                context_.logger);
        }

        OptError setPriority(const CyphalPriority priority) override
        {
            if (const auto error = completion_error_)
            {
                context_.logger->warn("NodeRpcClient::setPriority() Already completed with error (err={}).", *error);
                return error;
            }

            Spec::Request request{&context_.memory};
            auto&         config = request.set_config();
            config.priority.push_back(static_cast<std::uint8_t>(priority));

            const auto opt_error = channel_.send(request);
            if (opt_error)
            {
                context_.logger->warn("NodeRpcClient::setPriority() Failed to send 'config' request (err={}).",
                                      *opt_error);
            }
            return opt_error;
        }

    private:
        using SocketBuffer  = common::io::SocketBuffer;
        using CallRequest   = Spec::Request::_traits_::TypeOf::call;
        using ErrorResponse = Spec::Response::_traits_::TypeOf::_error;

        struct Requested
        {
            CallRequest         call;
            OwnedMutablePayload payload;
        };

        void handleEvent(const Channel::Input& input, const common::io::Payload payload)
        {
            context_.logger->trace("NodeRpcClient::handleEvent(Input).");

            cetl::visit(                //
                cetl::make_overloaded(  //
                    [this, payload](const auto& receive) {
                        //
                        handleInputEvent(receive, payload);
                    },
                    [](const uavcan::primitive::Empty_1_0&) {}),
                input.union_value);
        }

        void handleEvent(const Channel::Completed& completed)
        {
            context_.logger->debug("NodeRpcClient::handleEvent({}).", completed);
            completion_error_ = completed.opt_error.value_or(Error{Error::Code::Canceled});
            notifyReceived(Failure{*completion_error_});
        }

        void handleInputEvent(const common::svc::relay::RawRpcClientReceive_0_1& raw_receive,
                              const common::io::Payload                          payload) const
        {
#if defined(__cpp_exceptions)
            try
            {
#endif
                // The tail of the payload is the raw message data.
                // Copy the data as we pass it to the receiver, which might handle it asynchronously.
                //
                const auto raw_msg_payload = payload.subspan(payload.size() - raw_receive.payload_size);
                // NOLINTNEXTLINE(*-avoid-c-arrays)
                auto raw_msg_buff = std::make_unique<cetl::byte[]>(raw_msg_payload.size());
                std::memmove(raw_msg_buff.get(), raw_msg_payload.data(), raw_msg_payload.size());

                notifyReceived(RawCall::Success{{raw_msg_payload.size(), std::move(raw_msg_buff)},
                                                static_cast<CyphalPriority>(raw_receive.priority),
                                                raw_receive.remote_node_id});

#if defined(__cpp_exceptions)
            } catch (const std::bad_alloc&)
            {
                context_.logger->warn("NodeRpcClient::handleInputEvent() Cannot allocate message buffer.");
                notifyReceived(RawCall::Failure{Error::Code::OutOfMemory});
            }
#endif
        }

        void handleInputEvent(const ErrorResponse& response_error, const common::io::Payload) const
        {
            const auto opt_error = common::dsdlErrorToOptError(response_error);
            notifyReceived(RawCall::Failure{opt_error.value_or(Error{Error::Code::Other})});
        }

        void notifyReceived(RawCall::Result&& result) const
        {
            if (receiver_)
            {
                receiver_(std::move(result));
            }
        }

        const ClientContext                    context_;
        Channel                                channel_;
        Requested                              requested_;
        OptError                               completion_error_;
        std::function<void(RawCall::Result&&)> receiver_;

    };  // NodeRpcClientImpl

    void handleEvent(const Channel::Connected& connected)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->trace("RawRpcClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            context_.logger->warn("RawRpcClient::handleEvent() Failed to send request (err={}).", *opt_error);
            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input&)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->trace("RawRpcClient::handleEvent(Input).");

        auto node_rpc_client = std::make_shared<NodeRpcClientImpl>(context_, std::move(channel_));
        receiver_(Success{std::move(node_rpc_client)});
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->debug("RawRpcClient::handleEvent({}).", completed);

        receiver_(Failure{completed.opt_error.value_or(Error{Error::Code::Canceled})});
    }

    const ClientContext           context_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;

};  // RawRpcClientImpl

}  // namespace

RawRpcClient::Ptr RawRpcClient::make(const ClientContext& context, const Spec::Request& request)
{
    return std::make_shared<RawRpcClientImpl>(context, request);
}

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
