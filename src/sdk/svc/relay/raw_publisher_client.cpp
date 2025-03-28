//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "raw_publisher_client.hpp"

#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/node_pub_sub.hpp"
#include "svc/as_sender.hpp"

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
    RawPublisherClientImpl(cetl::pmr::memory_resource&           memory,
                           const common::ipc::ClientRouter::Ptr& ipc_router,
                           Spec::Request                         request)
        : memory_{memory}
        , logger_{common::getLogger("svc")}
        , request_{std::move(request)}
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

    class RawPublisherImpl final : public std::enable_shared_from_this<RawPublisherImpl>, public RawPublisher
    {
    public:
        RawPublisherImpl(common::LoggerPtr logger, Channel&& channel)
            : logger_(std::move(logger))
            , channel_{std::move(channel)}
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
                receiver(Failure{*error});
                return;
            }

            receiver_ = std::forward<Receiver>(receiver);
        }

        // RawPublisher

        SenderOf<OptError>::Ptr publish(const cetl::span<const cetl::byte>, const std::chrono::microseconds) override
        {
            return nullptr;
        }

        CyphalPriority getPriority() const override
        {
            return CyphalPriority::Nominal;
        }

        OptError setPriority(const CyphalPriority) override
        {
            return OptError{};
        }

    private:
        void handleEvent(const Channel::Input&)
        {
            logger_->trace("RawPublisher::handleEvent(Input).");
        }

        void handleEvent(const Channel::Completed& completed)
        {
            logger_->debug("RawPublisher::handleEvent({}).", completed);
            completion_error_ = completed.opt_error.value_or(Error{Error::Code::Canceled});
            notifyPublished(Failure{*completion_error_});
        }

        void notifyPublished(OptError opt_error)
        {
            if (receiver_)
            {
                receiver_(opt_error);
            }
        }

        common::LoggerPtr             logger_;
        Channel                       channel_;
        OptError                      completion_error_;
        std::function<void(OptError)> receiver_;

    };  // RawPublisherImpl

    void handleEvent(const Channel::Connected& connected)
    {
        logger_->trace("RawPublisherClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            CETL_DEBUG_ASSERT(receiver_, "");

            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input&)
    {
        logger_->trace("RawPublisherClient::handleEvent(Input).");

        auto raw_publisher = std::make_shared<RawPublisherImpl>(logger_, std::move(channel_));
        receiver_(Success{std::move(raw_publisher)});
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->debug("RawPublisherClient::handleEvent({}).", completed);
        receiver_(Failure{completed.opt_error.value_or(Error{Error::Code::Canceled})});
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;

};  // RawPublisherClientImpl

}  // namespace

RawPublisherClient::Ptr RawPublisherClient::make(  //
    cetl::pmr::memory_resource&           memory,
    const common::ipc::ClientRouter::Ptr& ipc_router,
    const Spec::Request&                  request)
{
    return std::make_shared<RawPublisherClientImpl>(memory, ipc_router, request);
}

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
