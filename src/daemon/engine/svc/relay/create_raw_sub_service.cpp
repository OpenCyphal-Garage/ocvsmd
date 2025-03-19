//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "create_raw_sub_service.hpp"

#include "logging.hpp"
#include "svc/relay/create_raw_sub_spec.hpp"

#include <cetl/pf17/cetlpf.hpp>

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

/// Defines 'Relay: Create Raw Subscriber' service implementation.
///
/// It's passed (as a functor) to the IPC server router to handle incoming service requests.
/// See `ipc::ServerRouter::registerChannel` for details, and below `operator()` for the actual implementation.
///
class CreateRawSubServiceImpl final
{
public:
    using Spec    = common::svc::relay::CreateRawSubSpec;
    using Channel = common::ipc::Channel<Spec::Request, Spec::Response>;

    explicit CreateRawSubServiceImpl(const ScvContext& context)
        : context_{context}
    {
    }

    /// Handles the initial `relay::CreateRawSub` service request of a new IPC channel.
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

        Fsm(CreateRawSubServiceImpl& service, const Id id, Channel&& channel)
            : id_{id}
            , channel_{std::move(channel)}
            , service_{service}
        {
            logger().trace("CreateRawSubSvc::Fsm (id={}).", id_);

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

        void handleEvent(const Channel::Input& input) {}

        void handleEvent(const Channel::Completed& completed)
        {
            logger().debug("CreateRawSubSvc::handleEvent({}) (fsm_id={}).", completed, id_);

            if (!completed.keep_alive)
            {
                logger().warn("CreateRawSubSvc: canceling processing (fsm_id={}).", id_);
                complete(sdk::Error{sdk::Error::Code::Canceled});
                return;
            }
        }

        void complete(const sdk::OptError completion_opt_error = {})
        {
            if (const auto opt_error = channel_.complete(completion_opt_error))
            {
                logger().warn("CreateRawSubSvc: failed to complete channel (err={}, fsm_id={}).", *opt_error, id_);
            }

            service_.releaseFsmBy(id_);
        }

        const Id                 id_;
        Channel                  channel_;
        CreateRawSubServiceImpl& service_;

    };  // Fsm

    void releaseFsmBy(const Fsm::Id fsm_id)
    {
        id_to_fsm_.erase(fsm_id);
    }

    const ScvContext                      context_;
    std::uint64_t                         next_fsm_id_{0};
    std::unordered_map<Fsm::Id, Fsm::Ptr> id_to_fsm_;
    common::LoggerPtr                     logger_{common::getLogger("engine")};

};  // CreateRawSubServiceImpl

}  // namespace

void CreateRawSubService::registerWithContext(const ScvContext& context)
{
    using Impl = CreateRawSubServiceImpl;

    context.ipc_router.registerChannel<Impl::Channel>(Impl::Spec::svc_full_name(), Impl{context});
}

}  // namespace relay
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
