//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "list_roots_client.hpp"

#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/defines.hpp"
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
    ListRootsClientImpl(cetl::pmr::memory_resource&           memory,
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

    void handleEvent(const Channel::Connected& connected)
    {
        logger_->trace("ListRootsClient::handleEvent({}).", connected);

        if (const auto opt_error = channel_.send(request_))
        {
            CETL_DEBUG_ASSERT(receiver_, "");

            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input& input)
    {
        logger_->trace("ListRootsClient::handleEvent(Input).");

        items_.emplace_back(input.item.path.begin(), input.item.path.end());
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->debug("ListRootsClient::handleEvent({}).", completed);
        receiver_(completed.opt_error ? Result{Failure{*completed.opt_error}} : Success{std::move(items_)});
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    Spec::Request                 request_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;
    std::vector<std::string>      items_;

};  // ListRootsClientImpl

}  // namespace

ListRootsClient::Ptr ListRootsClient::make(  //
    cetl::pmr::memory_resource&           memory,
    const common::ipc::ClientRouter::Ptr& ipc_router,
    const Spec::Request&                  request)
{
    return std::make_shared<ListRootsClientImpl>(memory, ipc_router, request);
}

}  // namespace file_server
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
