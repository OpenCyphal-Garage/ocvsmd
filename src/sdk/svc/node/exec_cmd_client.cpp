//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "exec_cmd_client.hpp"

#include "common_helpers.hpp"
#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "logging.hpp"
#include "svc/node/exec_cmd_spec.hpp"

#include <uavcan/node/ExecuteCommand_1_3.hpp>

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

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

class ExecCmdClientImpl final : public ExecCmdClient
{
public:
    ExecCmdClientImpl(cetl::pmr::memory_resource&           memory,
                      const common::ipc::ClientRouter::Ptr& ipc_router,
                      const CyphalNodeIds                   node_ids,
                      const NodeRequest&                    node_request,
                      const std::chrono::microseconds       timeout)
        : memory_{memory}
        , logger_{common::getLogger("svc")}
        , channel_{ipc_router->makeChannel<Channel>(Spec::svc_full_name())}
    {
        buildRequests(node_ids, node_request, timeout);
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
    using Channel = common::ipc::Channel<Spec::Response, Spec::Request>;

    void buildRequests(const CyphalNodeIds             node_ids,
                       const NodeRequest&              node_request,
                       const std::chrono::microseconds timeout)
    {
        const auto timeout_us = std::max<std::uint64_t>(0, timeout.count());

        // Split the whole span of node ids into chunks of `ArrayCapacity::node_ids` size.
        //
        constexpr std::size_t chunk_size = Spec::Request::_traits_::ArrayCapacity::node_ids;
        for (std::size_t offset = 0; offset < node_ids.size(); offset += chunk_size)
        {
            Spec::Request request{&memory_};
            request.timeout_us   = timeout_us;
            request.payload      = {node_request.command, node_request.parameter, &memory_};
            const auto ids_chunk = node_ids.subspan(offset, std::min(chunk_size, node_ids.size() - offset));
            std::copy(ids_chunk.begin(), ids_chunk.end(), std::back_inserter(request.node_ids));

            requests_.emplace_back(std::move(request));
        }
    }

    void handleEvent(const Channel::Connected& connected)
    {
        logger_->trace("ExecCmdClient::handleEvent({}).", connected);

        for (const auto& request : requests_)
        {
            if (const auto error_code = channel_.send(request))
            {
                CETL_DEBUG_ASSERT(receiver_, "");

                receiver_(Failure{*error_code});
                return;
            }
        }

        // Let the server know that all requests have been sent.
        //
        if (const auto error_code = channel_.complete(OptErrorCode{}, true))
        {
            receiver_(Failure{*error_code});
        }
    }

    void handleEvent(const Channel::Input& input)
    {
        logger_->trace("ExecCmdClient::handleEvent(Input).");

        if (const auto error_code = common::rawIntToOptErrorCode(input.error_code))
        {
            logger_->warn("ExecCmdClient::handleEvent(Input) - Node {} has failed (err={}).",
                          input.node_id,
                          *error_code);

            node_id_to_response_.emplace(input.node_id, NodeResponse::Failure{*error_code});
            return;
        }

        NodeResponse::Success node_response{input.payload.status, input.payload.output, &memory_};
        node_id_to_response_.emplace(input.node_id, std::move(node_response));
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->debug("ExecCmdClient::handleEvent({}).", completed);
        receiver_(completed.error_code ? Result{Failure{*completed.error_code}}
                                       : Success{std::move(node_id_to_response_)});
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    std::vector<Spec::Request>    requests_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;
    Success                       node_id_to_response_;

};  // ExecCmdClientImpl

}  // namespace

ExecCmdClient::Ptr ExecCmdClient::make(  //
    cetl::pmr::memory_resource&           memory,
    const common::ipc::ClientRouter::Ptr& ipc_router,
    const CyphalNodeIds                   node_ids,
    const NodeRequest&                    node_request,
    const std::chrono::microseconds       timeout)
{
    return std::make_shared<ExecCmdClientImpl>(memory, ipc_router, node_ids, node_request, timeout);
}

}  // namespace node
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
