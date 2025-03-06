//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "access_registers_service.hpp"

#include "engine_helpers.hpp"
#include "ipc/channel.hpp"
#include "ipc/server_router.hpp"
#include "logging.hpp"
#include "svc/node/access_registers_spec.hpp"
#include "svc/svc_helpers.hpp"

#include <uavcan/_register/Access_1_0.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/presentation/client.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/presentation/response_promise.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

/// Defines 'Node: Access Registers' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class AccessRegistersServiceImpl final
{
public:
    using Spec    = common::svc::node::AccessRegistersSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit AccessRegistersServiceImpl(const ScvContext& context)
        : context_{context}
    {
    }

    /// Handles the initial `node::AccessRegisters` service request of a new IPC channel.
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
    // Defines private Finite State Machine (FSM) which tracks the progress of a single IPC service request.
    // There is one FSM per each service request channel.
    //
    // 1. On its `start` a set of Cyphal RPC clients is created (one per each node ID in the request),
    //    and Cyphal `Access` request is sent to each of them.
    // 2. On RPC response reception FSM forwards a node response to the IPC client (which accumulates responses), and
    //    then repeats steps 1-2 for the incremented index in the registry list (until empty response from the node).
    // 3. Finally, when the working set of nodes becomes empty (all nodes returned their entire list of registers),
    //    FSM completes the channel.
    //
    class Fsm final
    {
    public:
        using Id  = std::uint64_t;
        using Ptr = std::shared_ptr<Fsm>;

        Fsm(AccessRegistersServiceImpl& service, const Id id, Channel&& channel)
            : id_{id}
            , channel_{std::move(channel)}
            , service_{service}
            , timeout_{}
        {
            logger().trace("AccessRegsSvc::Fsm (id={}).", id_);

            channel_.subscribe([this](const auto& event_var) {
                //
                cetl::visit([this](const auto& event) { handleEvent(event); }, event_var);
            });
        }

        ~Fsm()
        {
            logger().trace("AccessRegsSvc::~Fsm (id={}).", id_);
        }

        Fsm(const Fsm&)                = delete;
        Fsm(Fsm&&) noexcept            = delete;
        Fsm& operator=(const Fsm&)     = delete;
        Fsm& operator=(Fsm&&) noexcept = delete;

        void start(const Spec::Request&)
        {
            // TODO: Implement!
        }

    private:
        using SetOfNodeIds         = std::unordered_set<std::uint16_t>;
        using CyphalRegAccessSvc   = uavcan::_register::Access_1_0;
        using CyphalSvcClient      = libcyphal::presentation::ServiceClient<CyphalRegAccessSvc>;
        using CyphalPromise        = libcyphal::presentation::ResponsePromise<CyphalRegAccessSvc::Response>;
        using CyphalPromiseFailure = libcyphal::presentation::ResponsePromiseFailure;

        struct CyNodeOp
        {
            std::uint16_t                 index;
            CyphalSvcClient               client;
            cetl::optional<CyphalPromise> promise;
        };

        common::Logger& logger() const
        {
            return *service_.logger_;
        }

        cetl::pmr::memory_resource& memory() const
        {
            return service_.context_.memory;
        }

        // We are not interested in handling these events.
        static void handleEvent(const Channel::Connected&) {}
        static void handleEvent(const Channel::Input&) {}

        void handleEvent(const Channel::Completed& completed)
        {
            logger().debug("AccessRegsSvc::Fsm::handleEvent({}) (id={}).", completed, id_);
            complete(ECANCELED);
        }

        void makeCyNodeOp(const std::uint16_t node_id)
        {
            using CyphalMakeFailure = libcyphal::presentation::Presentation::MakeFailure;

            auto cy_make_result = service_.context_.presentation.makeClient<CyphalRegAccessSvc>(node_id);
            if (const auto* cy_failure = cetl::get_if<CyphalMakeFailure>(&cy_make_result))
            {
                const auto err = failureToErrorCode(*cy_failure);
                logger().error("AccessRegsSvc: failed to make svc client for node {} (err={}, fsm_id={}).",
                               node_id,
                               err,
                               id_);

                sendErrorResponse(node_id, err);
                return;
            }

            auto it = node_id_to_op_
                          .emplace(  //
                              node_id,
                              CyNodeOp{0, cetl::get<CyphalSvcClient>(std::move(cy_make_result)), cetl::nullopt})
                          .first;

            startCyRegAccessRpcCallFor(node_id, it->second);
        }

        bool startCyRegAccessRpcCallFor(const std::uint16_t, CyNodeOp&)
        {
            // TODO: Implement!
            return true;
        }

        void handleNodeResponse(const std::uint16_t node_id, const CyphalPromise::Result&)
        {
            const auto it = node_id_to_op_.find(node_id);
            if (it == node_id_to_op_.end())
            {
                return;
            }
            auto& cy_op = it->second;

            // TODO: Implement!
        }

        // TODO: Fix nolint by moving from `int` to `ErrorCode`.
        // NOLINTNEXTLINE bugprone-easily-swappable-parameters
        void sendErrorResponse(const std::uint16_t node_id, const int err_code)
        {
            Spec::Response ipc_response{&memory()};
            ipc_response.error_code = err_code;
            ipc_response.node_id    = node_id;

            if (const auto err = channel_.send(ipc_response))
            {
                logger().warn(  //
                    "AccessRegsSvc: failed to send ipc failure (err_code={}) response for node {} (err={}, fsm_id={}).",
                    err_code,
                    node_id,
                    err,
                    id_);
            }
        }

        void complete(const int err_code)
        {
            // Cancel anything that might be still pending.
            node_id_to_op_.clear();

            if (const auto err = channel_.complete(err_code))
            {
                logger().warn("AccessRegsSvc: failed to complete channel (err={}, fsm_id={}).", err, id_);
            }

            service_.releaseFsmBy(id_);
        }

        const Id                                    id_;
        Channel                                     channel_;
        AccessRegistersServiceImpl&                 service_;
        libcyphal::Duration                         timeout_;
        std::unordered_map<std::uint16_t, CyNodeOp> node_id_to_op_;

    };  // Fsm

    void releaseFsmBy(const Fsm::Id fsm_id)
    {
        id_to_fsm_.erase(fsm_id);
    }

    const ScvContext                      context_;
    std::uint64_t                         next_fsm_id_{0};
    std::unordered_map<Fsm::Id, Fsm::Ptr> id_to_fsm_;
    common::LoggerPtr                     logger_{common::getLogger("engine")};

};  // AccessRegistersServiceImpl

}  // namespace

void AccessRegistersService::registerWithContext(const ScvContext& context)
{
    using Impl = AccessRegistersServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context});
}

}  // namespace node
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
