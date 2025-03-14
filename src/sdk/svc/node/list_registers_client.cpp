//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "list_registers_client.hpp"

#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "svc/node/list_registers_spec.hpp"

#include <cetl/cetl.hpp>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace node
{
namespace
{

class ListRegistersClientImpl final : public ListRegistersClient
{
public:
    ListRegistersClientImpl(cetl::pmr::memory_resource&           memory,
                            const common::ipc::ClientRouter::Ptr& ipc_router,
                            const CyphalNodeIds                   node_ids,
                            const std::chrono::microseconds       timeout)
        : memory_{memory}
        , logger_{common::getLogger("svc")}
        , channel_{ipc_router->makeChannel<Channel>(Spec::svc_full_name())}
    {
        buildRequests(node_ids, timeout);
    }

    void submitImpl(std::function<void(Result&&)>&& receiver) override
    {
        receiver_ = std::move(receiver);

        channel_.subscribe([this](const auto& event_var) {
            //
            cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
        });
    }

private:
    using Channel       = common::ipc::Channel<Spec::Response, Spec::Request>;
    using NodeRegisters = NodeRegistryClient::List::NodeRegisters;

    void buildRequests(const CyphalNodeIds node_ids, const std::chrono::microseconds timeout)
    {
        const auto timeout_us = std::max<std::uint64_t>(0, timeout.count());

        // Split the whole span of node ids into chunks of `ArrayCapacity::node_ids` size.
        //
        constexpr std::size_t chunk_size = Spec::Request::_traits_::ArrayCapacity::node_ids;
        for (std::size_t offset = 0; offset < node_ids.size(); offset += chunk_size)
        {
            Spec::Request request{&memory_};
            request.timeout_us   = timeout_us;
            const auto ids_chunk = node_ids.subspan(offset, std::min(chunk_size, node_ids.size() - offset));
            std::copy(ids_chunk.begin(), ids_chunk.end(), std::back_inserter(request.node_ids));

            requests_.emplace_back(std::move(request));
        }
    }

    void handleEvent(const Channel::Connected& connected)
    {
        logger_->trace("ListRegistersClient::handleEvent({}).", connected);

        for (const auto& request : requests_)
        {
            const auto failure = channel_.send(request);
            if (failure != ErrorCode::Success)
            {
                CETL_DEBUG_ASSERT(receiver_, "");

                receiver_(failure);
                return;
            }
        }

        // Let the server know that all requests have been sent.
        //
        const auto failure = channel_.complete(ErrorCode::Success, true);
        if (failure != ErrorCode::Success)
        {
            receiver_(failure);
        }
    }

    void handleEvent(const Channel::Input& input)
    {
        logger_->trace("ListRegistersClient::handleEvent(Input).");

        if (input.error_code != 0)
        {
            logger_->warn("ListRegistersClient::handleEvent(Input) - Node {} has failed (err={}).",
                          input.node_id,
                          input.error_code);

            node_id_to_registers_.emplace(input.node_id, static_cast<ErrorCode>(input.error_code));
            return;
        }

        auto it = node_id_to_registers_.find(input.node_id);
        if (it == node_id_to_registers_.end())
        {
            it = node_id_to_registers_.insert(std::make_pair(input.node_id, NodeRegisters::Success{})).first;
        }

        if (auto* const regs = cetl::get_if<NodeRegisters::Success>(&it->second))
        {
            regs->emplace_back(input.item.name.begin(), input.item.name.end());
        }
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->debug("ListRegistersClient::handleEvent({}).", completed);

        if (completed.error_code != ErrorCode::Success)
        {
            receiver_(Failure{completed.error_code});
            return;
        }
        receiver_(std::move(node_id_to_registers_));
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    std::vector<Spec::Request>    requests_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;
    Success                       node_id_to_registers_;

};  // ListRegistersClientImpl

}  // namespace

ListRegistersClient::Ptr ListRegistersClient::make(  //
    cetl::pmr::memory_resource&           memory,
    const common::ipc::ClientRouter::Ptr& ipc_router,
    const CyphalNodeIds                   node_ids,
    const std::chrono::microseconds       timeout)
{
    return std::make_shared<ListRegistersClientImpl>(memory, ipc_router, node_ids, timeout);
}

}  // namespace node
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
