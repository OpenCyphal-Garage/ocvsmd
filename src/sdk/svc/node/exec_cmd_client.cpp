//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "exec_cmd_client.hpp"

#include "common_helpers.hpp"
#include "ipc/channel.hpp"
#include "logging.hpp"
#include "svc/client_helpers.hpp"
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
    ExecCmdClientImpl(const ClientContext&            context,
                      const CyphalNodeIds             node_ids,
                      const NodeRequest&              node_request,
                      const std::chrono::microseconds timeout)
        : context_{context}
        , channel_{context.ipc_router.makeChannel<Channel>(Spec::svc_full_name())}
    {
        buildRequests(node_ids, node_request, timeout);
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
            Spec::Request request{&context_.memory};
            request.timeout_us   = timeout_us;
            request.payload      = {node_request.command, node_request.parameter, &context_.memory};
            const auto ids_chunk = node_ids.subspan(offset, std::min(chunk_size, node_ids.size() - offset));
            std::copy(ids_chunk.begin(), ids_chunk.end(), std::back_inserter(request.node_ids));

            requests_.emplace_back(std::move(request));
        }
    }

    void handleEvent(const Channel::Connected& connected)
    {
        context_.logger->trace("ExecCmdClient::handleEvent({}).", connected);

        for (const auto& request : requests_)
        {
            if (const auto opt_error = channel_.send(request))
            {
                CETL_DEBUG_ASSERT(receiver_, "");

                receiver_(Failure{*opt_error});
                return;
            }
        }

        // Let the server know that all requests have been sent.
        //
        if (const auto opt_error = channel_.complete(OptError{}, true))
        {
            receiver_(Failure{*opt_error});
        }
    }

    void handleEvent(const Channel::Input& input)
    {
        context_.logger->trace("ExecCmdClient::handleEvent(Input).");

        if (const auto opt_error = dsdlErrorToOptError(input._error))
        {
            context_.logger->warn("ExecCmdClient::handleEvent(Input) - Node {} has failed (err={}).",
                                  input.node_id,
                                  *opt_error);

            node_id_to_response_.emplace(input.node_id, NodeResponse::Failure{*opt_error});
            return;
        }

        NodeResponse::Success node_response{input.payload.status, input.payload.output, &context_.memory};
        node_id_to_response_.emplace(input.node_id, std::move(node_response));
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        context_.logger->debug("ExecCmdClient::handleEvent({}).", completed);
        receiver_(completed.opt_error ? Result{Failure{*completed.opt_error}}
                                      : Success{std::move(node_id_to_response_)});
    }

    const ClientContext           context_;
    std::vector<Spec::Request>    requests_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;
    Success                       node_id_to_response_;

};  // ExecCmdClientImpl

}  // namespace

ExecCmdClient::Ptr ExecCmdClient::make(  //
    const ClientContext&            context,
    const CyphalNodeIds             node_ids,
    const NodeRequest&              node_request,
    const std::chrono::microseconds timeout)
{
    return std::make_shared<ExecCmdClientImpl>(context, node_ids, node_request, timeout);
}

}  // namespace node
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
