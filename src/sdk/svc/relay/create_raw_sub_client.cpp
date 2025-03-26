//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "create_raw_sub_client.hpp"
#include "io/socket_buffer.hpp"
#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "svc/as_sender.hpp"

#include <ocvsmd/common/svc/relay/RawMessage_0_1.hpp>
#include <uavcan/primitive/Empty_1_0.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/visit_helpers.hpp>

#include <memory>

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

class CreateRawSubClientImpl final : public CreateRawSubClient
{
public:
    CreateRawSubClientImpl(cetl::pmr::memory_resource&           memory,
                           const common::ipc::ClientRouter::Ptr& ipc_router,
                           const Spec::Request&                  request)
        : memory_{memory}
        , logger_{common::getLogger("svc")}
        , request_{request}
        , channel_{ipc_router->makeChannel<Channel>(Spec::svc_full_name())}
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

    class RawSubscriberImpl final : public std::enable_shared_from_this<RawSubscriberImpl>, public RawSubscriber
    {
    public:
        RawSubscriberImpl(common::LoggerPtr logger, Channel&& channel)
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
                receiver(Failure{*error});
            }

            receiver_ = std::forward<Receiver>(receiver);
        }

        // RawSubscriber

        SenderOf<Receive::Result>::Ptr receive() override
        {
            return std::make_unique<AsSender<Receive::Result, decltype(shared_from_this())>>(  //
                "RawSubscriber::receive",
                shared_from_this(),
                logger_);
        }

    private:
        void handleEvent(const Channel::Input& input_var, const common::io::Payload payload)
        {
            logger_->trace("RawSubscriber::handleEvent(Input).");

            cetl::visit(                //
                cetl::make_overloaded(  //
                    [this, payload](const auto& input) {
                        //
                        handleInputEvent(input, payload);
                    },
                    [](const uavcan::primitive::Empty_1_0&) {}),
                input_var.union_value);
        }

        void handleEvent(const Channel::Completed& completed)
        {
            logger_->debug("CreateRawSubClient::handleEvent({}).", completed);
            completion_error_ = completed.opt_error.value_or(Error::Code::Canceled);
            notifyReceived(Failure{*completion_error_});
        }

        void handleInputEvent(const common::svc::relay::RawMessage_0_1& raw_msg, const common::io::Payload payload)
        {
#if defined(__cpp_exceptions)
            try
            {
#endif
                // The tail of the payload is the raw message data.
                // Copy the data as we pass it to the receiver, which might handle it asynchronously.
                //
                const auto raw_msg_payload = payload.subspan(payload.size() - raw_msg.payload_size);
                // NOLINTNEXTLINE(*-avoid-c-arrays)
                auto raw_msg_buff = std::make_unique<cetl::byte[]>(raw_msg_payload.size());
                std::memmove(raw_msg_buff.get(), raw_msg_payload.data(), raw_msg_payload.size());

                const auto opt_node_id = raw_msg.remote_node_id.empty()
                                             ? cetl::nullopt
                                             : cetl::optional<CyphalNodeId>{raw_msg.remote_node_id.front()};

                notifyReceived(Receive::Success{raw_msg_payload.size(),
                                                std::move(raw_msg_buff),
                                                static_cast<CyphalPriority>(raw_msg.priority),
                                                opt_node_id});

#if defined(__cpp_exceptions)
            } catch (const std::bad_alloc&)
            {
                notifyReceived(Receive::Failure{Error::Code::OutOfMemory});
            }
#endif
        }

        void notifyReceived(Receive::Result&& result)
        {
            if (receiver_)
            {
                receiver_(std::move(result));
            }
        }

        common::LoggerPtr                      logger_;
        Channel                                channel_;
        OptError                               completion_error_;
        std::function<void(Receive::Result&&)> receiver_;

    };  // RawSubscriberImpl

    void handleEvent(const Channel::Connected& connected)
    {
        logger_->trace("CreateRawSubClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            CETL_DEBUG_ASSERT(receiver_, "");

            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input&)
    {
        logger_->trace("CreateRawSubClient::handleEvent(Input).");

        auto raw_subscriber = std::make_shared<RawSubscriberImpl>(logger_, std::move(channel_));
        receiver_(Success{std::move(raw_subscriber)});
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->debug("CreateRawSubClient::handleEvent({}).", completed);
        receiver_(Failure{completed.opt_error.value_or(Error::Code::Canceled)});
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;

};  // CreateRawSubClientImpl

}  // namespace

CreateRawSubClient::Ptr CreateRawSubClient::make(  //
    cetl::pmr::memory_resource&           memory,
    const common::ipc::ClientRouter::Ptr& ipc_router,
    const Spec::Request&                  request)
{
    return std::make_shared<CreateRawSubClientImpl>(memory, ipc_router, request);
}

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
