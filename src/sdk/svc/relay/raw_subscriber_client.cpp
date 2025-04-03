//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "raw_subscriber_client.hpp"

#include "io/socket_buffer.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/node_pub_sub.hpp"
#include "svc/client_helpers.hpp"

#include <ocvsmd/common/svc/relay/RawSubscriberReceive_0_1.hpp>
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

class RawSubscriberClientImpl final : public RawSubscriberClient
{
public:
    RawSubscriberClientImpl(const ClientContext& context, Spec::Request request)
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

    class SubscriberImpl final : public std::enable_shared_from_this<SubscriberImpl>, public Subscriber
    {
    public:
        SubscriberImpl(common::LoggerPtr logger, Channel&& channel)
            : logger_(std::move(logger))
            , channel_{std::move(channel)}
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
                logger_->warn("Subscriber::submit() Already completed with error (err={}).", *error);
                receiver(Failure{*error});
                return;
            }

            receiver_ = std::forward<Receiver>(receiver);
        }

        // RawSubscriber

        SenderOf<RawReceive::Result>::Ptr rawReceive() override
        {
            return std::make_unique<AsSender<RawReceive::Result, decltype(shared_from_this())>>(  //
                "Subscriber::rawReceive",
                shared_from_this(),
                logger_);
        }

    private:
        void handleEvent(const Channel::Input& input, const common::io::Payload payload)
        {
            logger_->trace("Subscriber::handleEvent(Input).");

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
            logger_->debug("Subscriber::handleEvent({}).", completed);
            completion_error_ = completed.opt_error.value_or(Error{Error::Code::Canceled});
            notifyReceived(Failure{*completion_error_});
        }

        void handleInputEvent(const common::svc::relay::RawSubscriberReceive_0_1& raw_receive,
                              const common::io::Payload                           payload) const
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

                const auto opt_node_id = raw_receive.remote_node_id.empty()
                                             ? cetl::nullopt
                                             : cetl::optional<CyphalNodeId>{raw_receive.remote_node_id.front()};

                notifyReceived(RawReceive::Success{{raw_msg_payload.size(), std::move(raw_msg_buff)},
                                                   static_cast<CyphalPriority>(raw_receive.priority),
                                                   opt_node_id});

#if defined(__cpp_exceptions)
            } catch (const std::bad_alloc&)
            {
                logger_->warn("Subscriber::handleInputEvent() Cannot allocate message buffer.");
                notifyReceived(RawReceive::Failure{Error::Code::OutOfMemory});
            }
#endif
        }

        void notifyReceived(RawReceive::Result&& result) const
        {
            if (receiver_)
            {
                receiver_(std::move(result));
            }
        }

        const common::LoggerPtr                   logger_;
        Channel                                   channel_;
        OptError                                  completion_error_;
        std::function<void(RawReceive::Result&&)> receiver_;

    };  // SubscriberImpl

    void handleEvent(const Channel::Connected& connected)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->trace("RawSubscriberClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            context_.logger->warn("RawSubscriberClient::handleEvent() Failed to send request (err={}).", *opt_error);
            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input&)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->trace("RawSubscriberClient::handleEvent(Input).");

        auto raw_subscriber = std::make_shared<SubscriberImpl>(context_.logger, std::move(channel_));
        receiver_(Success{std::move(raw_subscriber)});
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->debug("RawSubscriberClient::handleEvent({}).", completed);

        receiver_(Failure{completed.opt_error.value_or(Error{Error::Code::Canceled})});
    }

    const ClientContext           context_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;

};  // RawSubscriberClientImpl

}  // namespace

RawSubscriberClient::Ptr RawSubscriberClient::make(const ClientContext& context, const Spec::Request& request)
{
    return std::make_shared<RawSubscriberClientImpl>(context, request);
}

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
