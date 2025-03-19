//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "exec_cmd_service.hpp"

#include "common_helpers.hpp"
#include "engine_helpers.hpp"
#include "ipc/channel.hpp"
#include "ipc/server_router.hpp"
#include "logging.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "svc/node/exec_cmd_spec.hpp"
#include "svc/svc_helpers.hpp"

#include <uavcan/node/ExecuteCommand_1_3.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/presentation/client.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/presentation/response_promise.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace node
{
namespace
{

/// Defines 'Node: Execute Command' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class ExecCmdServiceImpl final
{
public:
    using Spec    = common::svc::node::ExecCmdSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit ExecCmdServiceImpl(const ScvContext& context)
        : context_{context}
    {
    }

    /// Handles the initial `node::ExecCmd` service request of a new IPC channel.
    ///
    /// Defined as a functor operator - as it's required/expected by the IPC server router.
    ///
    void operator()(Channel&& channel, const Spec::Request& request)
    {
        const auto fsm_id = next_fsm_id_++;
        logger_->debug("New '{}' service channel (fsm={}).", Spec::svc_full_name(), fsm_id);

        auto fsm           = std::make_shared<Fsm>(*this, fsm_id, std::move(channel));
        id_to_fsm_[fsm_id] = fsm;

        fsm->start(request);
    }

private:
    // Defines private Finite State Machine (FSM) which tracks the progress of a single service request.
    // There is one FSM per each service request channel.
    //
    // 1. On its `start` a set of Cyphal RPC clients is created (one per each node ID in the request),
    //    and a command request is sent to each of them.
    // 2. Then, the FSM waits for the RPC responses from the Cyphal nodes, collecting them.
    // 3. Finally, FSM sends the accumulated result (when all responses are received or timed out)
    //    and completes the channel.
    //
    class Fsm final
    {
    public:
        using Id  = std::uint64_t;
        using Ptr = std::shared_ptr<Fsm>;

        Fsm(ExecCmdServiceImpl& service, const Id id, Channel&& channel)
            : id_{id}
            , channel_{std::move(channel)}
            , service_{service}
            , processing_{false}
        {
            logger().trace("ExecCmdSvc::Fsm (id={}).", id_);

            channel_.subscribe([this](const auto& event_var) {
                //
                cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
            });
        }

        ~Fsm() = default;

        Fsm(const Fsm&)                = delete;
        Fsm(Fsm&&) noexcept            = delete;
        Fsm& operator=(const Fsm&)     = delete;
        Fsm& operator=(Fsm&&) noexcept = delete;

        void start(const Spec::Request& request)
        {
            handleEvent(request);
        }

    private:
        using RequestPayload  = common::svc::node::UavcanNodeExecCmdReq_0_1;
        using ResponsePayload = common::svc::node::UavcanNodeExecCmdRes_0_1;

        using CyExecCmdSvc     = uavcan::node::ExecuteCommand_1_3;
        using CySvcClient      = libcyphal::presentation::ServiceClient<CyExecCmdSvc>;
        using CyPromise        = libcyphal::presentation::ResponsePromise<CyExecCmdSvc::Response>;
        using CyPromiseFailure = libcyphal::presentation::ResponsePromiseFailure;

        struct NodeContext
        {
            libcyphal::Duration         timeout;
            RequestPayload              payload;
            cetl::optional<CySvcClient> client;
            cetl::optional<CyPromise>   promise;
        };

        common::Logger& logger() const
        {
            return *service_.logger_;
        }

        cetl::pmr::memory_resource& memory() const
        {
            return service_.context_.memory;
        }

        // We are not interested in handling this event.
        static void handleEvent(const Channel::Connected&) {}

        void handleEvent(const Channel::Input& input)
        {
            CETL_DEBUG_ASSERT(!processing_, "");
            if (processing_)
            {
                logger().warn("ExecCmdSvc: Ignoring extra input - already processing (fsm_id={}).", id_);
                return;
            }

            const auto timeout = std::chrono::duration_cast<libcyphal::Duration>(  //
                std::chrono::microseconds{input.timeout_us});

            for (const auto node_id : input.node_ids)
            {
                node_id_to_cnxt_.emplace(node_id, NodeContext{timeout, input.payload});
            }
        }

        void handleEvent(const Channel::Completed& completed)
        {
            logger().debug("ExecCmdSvc::handleEvent({}) (fsm_id={}).", completed, id_);

            if (!completed.keep_alive)
            {
                logger().warn("ExecCmdSvc: canceling processing (fsm_id={}).", id_);
                complete(sdk::Error{sdk::Error::Code::Canceled});
                return;
            }

            if (processing_)
            {
                logger().warn("ExecCmdSvc: Ignoring extra channel completion - already processing (fsm_id={}).", id_);
                return;
            }
            processing_ = true;

            if (node_id_to_cnxt_.empty())
            {
                logger().debug("ExecCmdSvc: Nothing to do - empty working set (fsm_id={}, nodes={}).",
                               id_,
                               node_id_to_cnxt_.size());
                complete();
                return;
            }

            // Below `makeCyRpcClient` call might modify `node_id_to_cnxt_`,
            // so we need to collect all node ids first.
            //
            std::vector<sdk::CyphalNodeId> node_ids;
            node_ids.reserve(node_id_to_cnxt_.size());
            for (const auto& pair : node_id_to_cnxt_)
            {
                node_ids.push_back(pair.first);
            }

            // For each node we try initiate execute command RPC call.
            //
            for (const auto node_id : node_ids)
            {
                makeCyRpcClient(node_id);
            }
        }

        void makeCyRpcClient(const sdk::CyphalNodeId node_id)
        {
            using CyMakeFailure = libcyphal::presentation::Presentation::MakeFailure;

            CETL_DEBUG_ASSERT(processing_, "");

            const auto it = node_id_to_cnxt_.find(node_id);
            if (it == node_id_to_cnxt_.end())
            {
                return;
            }
            auto& node_cnxt = it->second;

            auto cy_make_result = service_.context_.presentation.makeClient<CyExecCmdSvc>(node_id);
            if (const auto* const cy_failure = cetl::get_if<CyMakeFailure>(&cy_make_result))
            {
                const auto opt_error = cyFailureToOptError(*cy_failure);
                logger().warn("ExecCmdSvc: failed to make RPC client for node {} (err={}, fsm_id={}).",
                              node_id,
                              opt_error,
                              id_);

                sendResponse(node_id, ResponsePayload{&memory()}, opt_error);
                releaseNodeContext(node_id);
                return;
            }
            node_cnxt.client.emplace(cetl::get<CySvcClient>(std::move(cy_make_result)));

            startCyExecCmdRpcCallFor(node_id, node_cnxt);
        }

        void startCyExecCmdRpcCallFor(const sdk::CyphalNodeId node_id, NodeContext& node_cnxt)
        {
            CETL_DEBUG_ASSERT(processing_, "");
            CETL_DEBUG_ASSERT(node_cnxt.client, "");
            if (!node_cnxt.client)
            {
                releaseNodeContext(node_id);
                return;
            }

            const auto                  deadline = service_.context_.executor.now() + node_cnxt.timeout;
            const CyExecCmdSvc::Request cy_request{node_cnxt.payload.command, node_cnxt.payload.parameter, &memory()};

            auto cy_req_result = node_cnxt.client->request(deadline, cy_request);
            if (auto* const cy_failure = cetl::get_if<CySvcClient::Failure>(&cy_req_result))
            {
                const auto opt_error = cyFailureToOptError(*cy_failure);
                logger().warn("ExecCmdSvc: failed to send RPC request to node {} (err={}, fsm_id={})",
                              node_id,
                              opt_error,
                              id_);

                sendResponse(node_id, ResponsePayload{&memory()}, opt_error);
                releaseNodeContext(node_id);
                return;
            }

            auto cy_promise = cetl::get<CyPromise>(std::move(cy_req_result));
            cy_promise.setCallback([this, node_id](const auto& arg) {
                //
                handleNodeResponse(node_id, arg.result);
            });
            node_cnxt.promise.emplace(std::move(cy_promise));
        }

        void handleNodeResponse(const sdk::CyphalNodeId node_id, const CyPromise::Result& result)
        {
            CETL_DEBUG_ASSERT(processing_, "");

            sdk::OptError   opt_error{};
            ResponsePayload payload{&memory()};
            //
            if (const auto* const success = cetl::get_if<CyPromise::Success>(&result))
            {
                const auto& res = success->response;
                logger().debug("ExecCmdSvc: RPC promise success from node {} (status={}, fsm_id={}).",
                               node_id,
                               res.status,
                               id_);

                payload.status = res.status;
                payload.output = res.output;
            }
            else if (const auto* const cy_failure = cetl::get_if<CyPromiseFailure>(&result))
            {
                opt_error = cyFailureToOptError(*cy_failure);
                logger().warn("ExecCmdSvc: RPC promise failure for node {} (err={}, fsm_id={}).",
                              node_id,
                              opt_error,
                              id_);
            }
            sendResponse(node_id, payload, opt_error);

            releaseNodeContext(node_id);
        }

        void sendResponse(const sdk::CyphalNodeId node_id,
                          const ResponsePayload&  payload,
                          const sdk::OptError     opt_error = {})
        {
            Spec::Response ipc_response{&memory()};
            ipc_response.node_id = node_id;
            ipc_response.payload = payload;
            optErrorToDsdlError(opt_error, ipc_response._error);

            if (const auto send_opt_error = channel_.send(ipc_response))
            {
                logger().warn("ExecCmdSvc: failed to send ipc response for node {} (err={}, fsm_id={}).",
                              node_id,
                              *send_opt_error,
                              id_);
            }
        }

        void releaseNodeContext(const sdk::CyphalNodeId node_id)
        {
            node_id_to_cnxt_.erase(node_id);
            if (node_id_to_cnxt_.empty())
            {
                complete();
            }
        }

        void complete(const sdk::OptError completion_opt_error = {})
        {
            // Cancel anything that might be still pending.
            node_id_to_cnxt_.clear();

            if (const auto opt_error = channel_.complete(completion_opt_error))
            {
                logger().warn("ExecCmdSvc: failed to complete channel (err={}, fsm_id={}).", *opt_error, id_);
            }

            service_.releaseFsmBy(id_);
        }

        const Id                                           id_;
        Channel                                            channel_;
        ExecCmdServiceImpl&                                service_;
        bool                                               processing_;
        std::unordered_map<sdk::CyphalNodeId, NodeContext> node_id_to_cnxt_;

    };  // Fsm

    void releaseFsmBy(const Fsm::Id fsm_id)
    {
        id_to_fsm_.erase(fsm_id);
    }

    const ScvContext                      context_;
    std::uint64_t                         next_fsm_id_{0};
    std::unordered_map<Fsm::Id, Fsm::Ptr> id_to_fsm_;
    common::LoggerPtr                     logger_{common::getLogger("engine")};

};  // ExecCmdServiceImpl

}  // namespace

void ExecCmdService::registerWithContext(const ScvContext& context)
{
    using Impl = ExecCmdServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context});
}

}  // namespace node
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
