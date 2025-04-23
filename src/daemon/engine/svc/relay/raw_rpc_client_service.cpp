//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "raw_rpc_client_service.hpp"

#include "common_helpers.hpp"
#include "engine_helpers.hpp"
#include "io/socket_buffer.hpp"
#include "logging.hpp"
#include "svc/relay/raw_rpc_client_spec.hpp"
#include "svc/svc_helpers.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/visit_helpers.hpp>
#include <libcyphal/presentation/client.hpp>
#include <libcyphal/presentation/presentation.hpp>

#include <array>
#include <memory>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace relay
{
namespace
{

/// Defines 'Relay: Raw RPC Client' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class RawRpcClientServiceImpl final
{
public:
    using Spec    = common::svc::relay::RawRpcClientSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit RawRpcClientServiceImpl(const ScvContext& context)
        : context_{context}
    {
    }

    /// Handles the initial `relay::RawRpcClient` service request of a new IPC channel.
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
    class Fsm final
    {
    public:
        using Id  = std::uint64_t;
        using Ptr = std::shared_ptr<Fsm>;

        Fsm(RawRpcClientServiceImpl& service, const Id id, Channel&& channel)
            : id_{id}
            , channel_{std::move(channel)}
            , service_{service}
        {
            logger().trace("RawRpcClientSvc::Fsm (id={}).", id_);

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

        ~Fsm() = default;

        Fsm(const Fsm&)                = delete;
        Fsm(Fsm&&) noexcept            = delete;
        Fsm& operator=(const Fsm&)     = delete;
        Fsm& operator=(Fsm&&) noexcept = delete;

        void start(const Spec::Request& request)
        {
            constexpr auto CreateReq = Spec::Request::VariantType::IndexOf::create;

            if (const auto* const create_req = cetl::get_if<CreateReq>(&request.union_value))
            {
                const auto& req = *create_req;
                logger().trace("RawRpcClientSvc::start (node_id={}, svc_id={}, fsm_id={}).",
                               req.server_node_id,
                               req.service_id,
                               id_);

                if (makeCySvcClient(req.server_node_id, req.service_id, req.extent_size))
                {
                    const Spec::Response ipc_response{&memory()};
                    reply(ipc_response, true);
                }
            }
        }

    private:
        using RawRpcClientCreate = common::svc::relay::RawRpcClientCreate_0_1;
        using RawRpcClientConfig = common::svc::relay::RawRpcClientConfig_0_1;
        using RawRpcClientCall   = common::svc::relay::RawRpcClientCall_0_1;

        using CyPayloadFragment = libcyphal::transport::PayloadFragment;
        using CyRawSvcClient    = libcyphal::presentation::RawServiceClient;
        using CyPromise         = libcyphal::presentation::ResponsePromise<void>;
        using CyPromiseFailure  = libcyphal::presentation::RawResponsePromiseFailure;

        common::Logger& logger() const
        {
            return *service_.logger_;
        }

        cetl::pmr::memory_resource& memory() const
        {
            return service_.context_.memory;
        }

        void handleEvent(const Channel::Input& input, const common::io::Payload payload)
        {
            logger().trace("RawRpcClientSvc::handleEvent(Input).");

            cetl::visit(                //
                cetl::make_overloaded(  //
                    [this](const RawRpcClientConfig& config) {
                        //
                        handleInputEvent(config);
                    },
                    [this, payload](const RawRpcClientCall& call) {
                        //
                        handleInputEvent(call, payload);
                    },
                    [](const RawRpcClientCreate&) {},
                    [](const uavcan::primitive::Empty_1_0&) {}),
                input.union_value);
        }

        void handleEvent(const Channel::Completed& completed)
        {
            logger().debug("RawRpcClientSvc::handleEvent({}) (fsm_id={}).", completed, id_);

            if (!completed.keep_alive)
            {
                complete(completed.opt_error);
            }
        }

        void handleInputEvent(const common::svc::relay::RawRpcClientConfig_0_1& config)
        {
            logger().trace("RawRpcClientSvc::handleInputEvent config (fsm_id={}).", id_);

            CETL_DEBUG_ASSERT(cy_raw_svc_client_, "");
            if (!cy_raw_svc_client_)
            {
                complete(sdk::Error{sdk::Error::Code::Canceled});
                return;
            }

            if (!config.priority.empty())
            {
                const auto priority = config.priority.front();
                logger().debug("RawRpcClientSvc::handleInputEvent: setPriority={} (fsm_id={}).", priority, id_);
                cy_raw_svc_client_->setPriority(convertToCyPriority(priority));
            }
        }

        void handleInputEvent(const common::svc::relay::RawRpcClientCall_0_1& call, const common::io::Payload payload)
        {
            logger().trace("RawRpcClientSvc::handleInputEvent call (fsm_id={}).", id_);

            CETL_DEBUG_ASSERT(cy_raw_svc_client_, "");
            if (!cy_raw_svc_client_)
            {
                complete(sdk::Error{sdk::Error::Code::Canceled});
                return;
            }

            const auto now               = service_.context_.executor.now();
            const auto request_deadline  = now + std::chrono::duration_cast<libcyphal::Duration>(  //
                                                    std::chrono::microseconds{call.request_timeout_us});
            const auto response_deadline = now + std::chrono::duration_cast<libcyphal::Duration>(
                                                     std::chrono::microseconds{call.response_timeout_us});

            // The tail of the payload is the raw message data.
            //
            const auto                       raw_msg_payload = payload.subspan(payload.size() - call.payload_size);
            std::array<CyPayloadFragment, 1> fragments{{{raw_msg_payload.data(), raw_msg_payload.size()}}};

            auto cy_req_result = cy_raw_svc_client_->request(request_deadline,
                                                             fragments,
                                                             cetl::optional<libcyphal::TimePoint>{response_deadline});
            if (auto* const cy_failure = cetl::get_if<CyRawSvcClient::Failure>(&cy_req_result))
            {
                const auto opt_error = cyFailureToOptError(*cy_failure);
                logger().warn("RawRpcClientSvc: failed to send raw request (err={}, fsm_id={})", opt_error, id_);

                Spec::Response ipc_response{&memory()};
                auto&          error = ipc_response.set__error();
                optErrorToDsdlError(opt_error, error);
                reply(ipc_response);
                return;
            }

            auto cy_promise = cetl::get<CyPromise>(std::move(cy_req_result));
            cy_promise.setCallback([this](const auto& arg) {
                //
                handleRpcResponse(arg.result);
            });
            cy_promise_.emplace(std::move(cy_promise));
        }

        bool makeCySvcClient(const sdk::CyphalNodeId server_node_id,
                             const sdk::CyphalPortId service_id,
                             const std::size_t       extent_bytes)
        {
            using CyMakeFailure = libcyphal::presentation::Presentation::MakeFailure;

            auto cy_make_result = service_.context_.presentation.makeClient(server_node_id, service_id, extent_bytes);
            if (const auto* const cy_failure = cetl::get_if<CyMakeFailure>(&cy_make_result))
            {
                const auto opt_error = cyFailureToOptError(*cy_failure);
                logger().warn("RawRpcClientSvc: failed to make RPC client (svc_id={}, err={}, fsm_id={}).",
                              service_id,
                              opt_error,
                              id_);

                complete(opt_error);
                return false;
            }

            cy_raw_svc_client_.emplace(cetl::get<CyRawSvcClient>(std::move(cy_make_result)));
            return true;
        }

        void handleRpcResponse(const CyPromise::Result& result)
        {
            if (const auto* const success = cetl::get_if<CyPromise::Success>(&result))
            {
                logger().debug("RawRpcClientSvc: RPC promise success from node {} (fsm_id={}).",
                               success->metadata.remote_node_id,
                               id_);

                Spec::Response ipc_response{&memory()};
                auto&          receive = ipc_response.set_receive();
                receive.priority       = static_cast<std::uint8_t>(success->metadata.rx_meta.base.priority);
                receive.remote_node_id = success->metadata.remote_node_id;
                receive.payload_size   = success->response.size();
                common::io::SocketBuffer sock_buff{success->response};
                reply(ipc_response, sock_buff);
            }
            else if (const auto* const cy_failure = cetl::get_if<CyPromiseFailure>(&result))
            {
                const auto opt_error = cyFailureToOptError(*cy_failure);
                logger().warn("RawRpcClientSvc: RPC promise failure (err={}, fsm_id={}).", opt_error, id_);

                Spec::Response ipc_response{&memory()};
                auto&          error = ipc_response.set__error();
                optErrorToDsdlError(opt_error, error);
                reply(ipc_response);
            }
        }

        void reply(const Spec::Response& ipc_response, const bool complete_of_failure = false)
        {
            if (const auto opt_error = channel_.send(ipc_response))
            {
                logger().warn("RawRpcClientSvc: failed to send ipc response (err={}, fsm_id={}).", *opt_error, id_);

                if (complete_of_failure)
                {
                    complete(opt_error);
                }
            }
        }

        void reply(const Spec::Response& ipc_response, common::io::SocketBuffer& sock_buff)
        {
            if (const auto opt_error = channel_.send(ipc_response, sock_buff))
            {
                logger().warn("RawRpcClientSvc: failed to send ipc response (err={}, fsm_id={}).", *opt_error, id_);
            }
        }

        void complete(const sdk::OptError completion_opt_error = {})
        {
            cy_promise_.reset();
            cy_raw_svc_client_.reset();

            if (const auto opt_error = channel_.complete(completion_opt_error))
            {
                logger().warn("RawRpcClientSvc: failed to complete channel (err={}, fsm_id={}).", *opt_error, id_);
            }

            service_.releaseFsmBy(id_);
        }

        static libcyphal::transport::Priority convertToCyPriority(const std::uint8_t raw_priority)
        {
            return static_cast<libcyphal::transport::Priority>(raw_priority);
        }

        const Id                       id_;
        Channel                        channel_;
        RawRpcClientServiceImpl&       service_;
        cetl::optional<CyRawSvcClient> cy_raw_svc_client_;
        cetl::optional<CyPromise>      cy_promise_;

    };  // Fsm

    void releaseFsmBy(const Fsm::Id fsm_id)
    {
        id_to_fsm_.erase(fsm_id);
    }

    const ScvContext                      context_;
    std::uint64_t                         next_fsm_id_{0};
    std::unordered_map<Fsm::Id, Fsm::Ptr> id_to_fsm_;
    common::LoggerPtr                     logger_{common::getLogger("engine")};

};  // RawRpcClientServiceImpl

}  // namespace

void RawRpcClientService::registerWithContext(const ScvContext& context)
{
    using Impl = RawRpcClientServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context});
}

}  // namespace relay
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
