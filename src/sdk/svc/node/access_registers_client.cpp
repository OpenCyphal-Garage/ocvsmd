//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "access_registers_client.hpp"

#include "common_helpers.hpp"
#include "ipc/channel.hpp"
#include "ipc/client_router.hpp"
#include "ipc/ipc_types.hpp"
#include "logging.hpp"
#include "ocvsmd/common/svc/node/AccessRegistersScope_0_1.hpp"
#include "svc/node/access_registers_spec.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

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

class AccessRegistersClientImpl final : public AccessRegistersClient
{
public:
    AccessRegistersClientImpl(cetl::pmr::memory_resource&               memory,
                              const common::ipc::ClientRouter::Ptr&     ipc_router,
                              const CyphalNodeIds                       node_ids,
                              const cetl::span<const cetl::string_view> registers,
                              const std::chrono::microseconds           timeout)
        : memory_{memory}
        , logger_{common::getLogger("svc")}
        , channel_{ipc_router->makeChannel<Channel>(Spec::svc_full_name())}
    {
        buildScopeRequests(node_ids, timeout);
        buildReadRegisterRequests(registers);
    }

    AccessRegistersClientImpl(cetl::pmr::memory_resource&           memory,
                              const common::ipc::ClientRouter::Ptr& ipc_router,
                              const CyphalNodeIds                   node_ids,
                              const cetl::span<const RegKeyValue>   registers,
                              const std::chrono::microseconds       timeout)
        : memory_{memory}
        , logger_{common::getLogger("svc")}
        , channel_{ipc_router->makeChannel<Channel>(Spec::svc_full_name())}
    {
        buildScopeRequests(node_ids, timeout);
        buildWriteRegisterRequests(registers);
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
    using Channel          = common::ipc::Channel<Spec::Response, Spec::Request>;
    using NodeRegisters    = NodeRegistryClient::Access::NodeRegisters;
    using RegKeyValueOrErr = NodeRegistryClient::Access::RegKeyValueOrErr;

    void buildScopeRequests(const CyphalNodeIds node_ids, const std::chrono::microseconds timeout)
    {
        using ScopeReq = Spec::Request::_traits_::TypeOf::scope;

        const auto timeout_us = std::max<std::uint64_t>(0, timeout.count());

        // Split the whole span of node ids into chunks of `ArrayCapacity::node_ids` size.
        //
        constexpr std::size_t chunk_size = ScopeReq::_traits_::ArrayCapacity::node_ids;
        for (std::size_t offset = 0; offset < node_ids.size(); offset += chunk_size)
        {
            Spec::Request request{&memory_};
            ScopeReq&     scope_req = request.set_scope();

            scope_req.timeout_us = timeout_us;
            const auto ids_chunk = node_ids.subspan(offset, std::min(chunk_size, node_ids.size() - offset));
            std::copy(ids_chunk.begin(), ids_chunk.end(), std::back_inserter(scope_req.node_ids));

            requests_.emplace_back(std::move(request));
        }
    }

    void buildReadRegisterRequests(const cetl::span<const cetl::string_view> registers)
    {
        using RegisterReq = Spec::Request::_traits_::TypeOf::_register;

        // For each register append separate request with its key.
        //
        for (const auto& reg_key : registers)
        {
            Spec::Request request{&memory_};
            RegisterReq&  register_req = request.set__register();

            std::copy(reg_key.cbegin(), reg_key.cend(), std::back_inserter(register_req.key.name));

            requests_.emplace_back(std::move(request));
        }
    }

    void buildWriteRegisterRequests(const cetl::span<const RegKeyValue> registers)
    {
        using RegisterReq = Spec::Request::_traits_::TypeOf::_register;

        // For each register append separate request with its key and value.
        //
        for (const auto& reg : registers)
        {
            Spec::Request request{&memory_};
            RegisterReq&  register_req = request.set__register();

            std::copy(reg.key.cbegin(), reg.key.cend(), std::back_inserter(register_req.key.name));
            register_req.value = reg.value;

            requests_.emplace_back(std::move(request));
        }
    }

    void handleEvent(const Channel::Connected& connected)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->trace("AccessRegistersClient::handleEvent({}).", connected);

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
        logger_->trace("AccessRegistersClient::handleEvent(Input).");

        const auto opt_error = dsdlErrorToOptError(input._error);
        if (opt_error && input._register.key.name.empty())
        {
            logger_->warn("AccessRegistersClient::handleEvent(Input) - Node {} has failed (err={}).",
                          input.node_id,
                          *opt_error);

            node_id_to_reg_vals_.emplace(input.node_id, NodeRegisters::Failure{*opt_error});
            return;
        }

        auto it = node_id_to_reg_vals_.find(input.node_id);
        if (it == node_id_to_reg_vals_.end())
        {
            it = node_id_to_reg_vals_.insert(std::make_pair(input.node_id, NodeRegisters::Success{})).first;
        }

        std::string reg_key{input._register.key.name.begin(), input._register.key.name.end()};
        if (auto* const regs = cetl::get_if<NodeRegisters::Success>(&it->second))
        {
            if (opt_error)
            {
                regs->emplace_back(RegKeyValueOrErr{std::move(reg_key), *opt_error});
            }
            else
            {
                regs->emplace_back(RegKeyValueOrErr{std::move(reg_key), input._register.value});
            }
        }
    }

    void handleEvent(const Channel::Completed& completed)
    {
        CETL_DEBUG_ASSERT(receiver_, "");

        logger_->debug("AccessRegistersClient::handleEvent({}).", completed);
        receiver_(completed.opt_error ? Result{Failure{*completed.opt_error}}
                                      : Success{std::move(node_id_to_reg_vals_)});
    }

    cetl::pmr::memory_resource&   memory_;
    common::LoggerPtr             logger_;
    std::vector<Spec::Request>    requests_;
    Channel                       channel_;
    std::function<void(Result&&)> receiver_;
    Success                       node_id_to_reg_vals_;

};  // AccessRegistersClientImpl

}  // namespace

AccessRegistersClient::Ptr AccessRegistersClient::make(  //
    cetl::pmr::memory_resource&               memory,
    const common::ipc::ClientRouter::Ptr&     ipc_router,
    const CyphalNodeIds                       node_ids,
    const cetl::span<const cetl::string_view> registers,
    const std::chrono::microseconds           timeout)
{
    return std::make_shared<AccessRegistersClientImpl>(memory, ipc_router, node_ids, registers, timeout);
}

AccessRegistersClient::Ptr AccessRegistersClient::make(  //
    cetl::pmr::memory_resource&           memory,
    const common::ipc::ClientRouter::Ptr& ipc_router,
    const CyphalNodeIds                   node_ids,
    const cetl::span<const RegKeyValue>   registers,
    const std::chrono::microseconds       timeout)
{
    return std::make_shared<AccessRegistersClientImpl>(memory, ipc_router, node_ids, registers, timeout);
}

}  // namespace node
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd
