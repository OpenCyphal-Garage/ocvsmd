//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "list_roots_client.hpp"

#include "ipc/channel.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "svc/client_helpers.hpp"
#include "svc/file_server/list_roots_spec.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

/// Defines implementation of the 'File Server: List Roots' service client.
///
class ListRootsClientImpl final : public ListRootsClient
{
public:
    ListRootsClientImpl(const ClientContext& context, const Spec::Request& request)
        : context_{context}
        , request_{request}
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
        context_.logger->trace("ListRootsClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            CETL_DEBUG_ASSERT(receiver_, "");

            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input& input)
    {
        context_.logger->trace("ListRootsClient::handleEvent(Input).");

        items_.emplace_back(input.item.path.begin(), input.item.path.end());
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->debug("ListRootsClient::handleEvent({}).", completed);
        receiver_(completed.opt_error ? Result{Failure{*completed.opt_error}} : Success{std::move(items_)});
    }

    const ClientContext           context_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;
    std::vector<std::string>      items_;

};  // ListRootsClientImpl

}  // namespace

ListRootsClient::Ptr ListRootsClient::make(const ClientContext& context, const Spec::Request& request)
{
    return std::make_shared<ListRootsClientImpl>(context, request);
}

}  // namespace file_server
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
