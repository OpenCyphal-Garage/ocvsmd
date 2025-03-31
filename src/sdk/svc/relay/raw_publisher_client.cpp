//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "raw_publisher_client.hpp"

#include "logging.hpp"
#include "ocvsmd/sdk/execution.hpp"
#include "ocvsmd/sdk/node_pub_sub.hpp"
#include "svc/client_helpers.hpp"

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

class RawPublisherClientImpl final : public RawPublisherClient
{
public:
    RawPublisherClientImpl(const ClientContext& context, Spec::Request request)
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

    class PublisherImpl final : public std::enable_shared_from_this<PublisherImpl>, public Publisher
    {
    public:
        PublisherImpl(const ClientContext& context, Channel&& channel)
            : context_{context}
            , channel_{std::move(channel)}
            , published_{PublishRequest{&context.memory}, {}}
        {
            channel_.subscribe([this](const auto& event_var, const auto) {
                //
                cetl::visit(                //
                    cetl::make_overloaded(  //
                        [this](const Channel::Input& input) {
                            //
                            handleEvent(input);
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
                context_.logger->warn("Publisher::submit() Already completed with error (err={}).", *error);
                receiver(OptError{error});
                return;
            }

            Spec::Request request{&context_.memory};
            auto&         publish = request.set_publish(published_.request);
            publish.payload_size  = published_.payload.size;
            SocketBuffer sock_buff{{published_.payload.data.get(), publish.payload_size}};
            const auto   opt_send_error = channel_.send(request, sock_buff);

            // Raw message payload has been sent, so no need to keep it in memory.
            published_.payload.size = 0;
            published_.payload.data.reset();

            if (const auto error = opt_send_error)
            {
                context_.logger->warn("Publisher::submit() Failed to send 'publish' request (err={}).", *error);
                receiver(OptError{error});
                return;
            }

            receiver_ = std::forward<Receiver>(receiver);
        }

        // Publisher

        SenderOf<OptError>::Ptr rawPublish(OwnMutablePayload&&             raw_payload,
                                           const std::chrono::microseconds timeout) override
        {
            published_.payload            = std::move(raw_payload);
            published_.request.timeout_us = std::max<std::uint64_t>(0, timeout.count());

            return std::make_unique<AsSender<OptError, decltype(shared_from_this())>>(  //
                "Publisher::rawPublish",
                shared_from_this(),
                context_.logger);
        }

        OptError setPriority(const CyphalPriority priority) override
        {
            if (const auto error = completion_error_)
            {
                context_.logger->warn("Publisher::setPriority() Already completed with error (err={}).", *error);
                return error;
            }

            Spec::Request request{&context_.memory};
            auto&         config = request.set_config();
            config.priority.push_back(static_cast<std::uint8_t>(priority));

            const auto opt_error = channel_.send(request);
            if (opt_error)
            {
                context_.logger->warn("Publisher::setPriority() Failed to send 'config' request (err={}).", *opt_error);
            }
            return opt_error;
        }

    private:
        using SocketBuffer    = common::io::SocketBuffer;
        using PublishRequest  = Spec::Request::_traits_::TypeOf::publish;
        using PublishResponse = Spec::Response::_traits_::TypeOf::publish_error;

        struct Published
        {
            PublishRequest    request;
            OwnMutablePayload payload;  // NOLINT(*-avoid-c-arrays)
        };

        void handleEvent(const Channel::Input& input)
        {
            context_.logger->trace("Publisher::handleEvent(Input).");

            cetl::visit(                //
                cetl::make_overloaded(  //
                    [this](const auto& published) {
                        //
                        handleInputEvent(published);
                    },
                    [](const uavcan::primitive::Empty_1_0&) {}),
                input.union_value);
        }

        void handleEvent(const Channel::Completed& completed)
        {
            context_.logger->debug("Publisher::handleEvent({}).", completed);
            completion_error_ = completed.opt_error.value_or(Error{Error::Code::Canceled});
            notifyPublished(completion_error_);
        }

        void handleInputEvent(const PublishResponse& publish_error) const
        {
            notifyPublished(dsdlErrorToOptError(publish_error));
        }

        void notifyPublished(const OptError opt_error) const
        {
            if (receiver_)
            {
                receiver_(opt_error);
            }
        }

        const ClientContext           context_;
        Channel                       channel_;
        Published                     published_;
        OptError                      completion_error_;
        std::function<void(OptError)> receiver_;

    };  // PublisherImpl

    void handleEvent(const Channel::Connected& connected)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->trace("RawPublisherClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            context_.logger->warn("RawPublisherClient::handleEvent() Failed to send request (err={}).", *opt_error);
            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input&)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->trace("RawPublisherClient::handleEvent(Input).");

        auto raw_publisher = std::make_shared<PublisherImpl>(context_, std::move(channel_));
        receiver_(Success{std::move(raw_publisher)});
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->debug("RawPublisherClient::handleEvent({}).", completed);

        receiver_(Failure{completed.opt_error.value_or(Error{Error::Code::Canceled})});
    }

    const ClientContext           context_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;

};  // RawPublisherClientImpl

}  // namespace

RawPublisherClient::Ptr RawPublisherClient::make(const ClientContext& context, const Spec::Request& request)
{
    return std::make_shared<RawPublisherClientImpl>(context, request);
}

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
