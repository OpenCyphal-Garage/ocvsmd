//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "push_root_client.hpp"

#include "ipc/channel.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "svc/client_helpers.hpp"
#include "svc/file_server/push_root_spec.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace file_server
{
namespace
{

/// Defines implementation of the 'File Server: Push Root' service client.
///
class PushRootClientImpl final : public PushRootClient
{
public:
    PushRootClientImpl(const ClientContext& context, Spec::Request request)
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

    void handleEvent(const Channel::Connected& connected)
    {
        context_.logger->trace("PushRootClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            CETL_DEBUG_ASSERT(receiver_, "");

            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input&) const
    {
        context_.logger->trace("PushRootClient::handleEvent(Input).");
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        receiver_(completed.opt_error ? Result{Failure{*completed.opt_error}} : Success{});
    }

    const ClientContext           context_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;

};  // PushRootClientImpl

}  // namespace

PushRootClient::Ptr PushRootClient::make(const ClientContext& context, const Spec::Request& request)
{
    return std::make_shared<PushRootClientImpl>(context, request);
}

}  // namespace file_server
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
