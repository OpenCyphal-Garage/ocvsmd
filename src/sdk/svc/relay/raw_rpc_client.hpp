//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_RELAY_RAW_RPC_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_SVC_RELAY_RAW_RPC_CLIENT_HPP_INCLUDED

#include "ocvsmd/sdk/defines.hpp"
#include "ocvsmd/sdk/node_rpc_client.hpp"
#include "svc/client_helpers.hpp"
#include "svc/relay/raw_rpc_client_spec.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <memory>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace relay
{

/// Defines interface of the 'Relay: Raw RPC Client' service client.
///
class RawRpcClient
{
public:
    using Ptr  = std::shared_ptr<RawRpcClient>;
    using Spec = common::svc::relay::RawRpcClientSpec;

    using Success = NodeRpcClient::Ptr;
    using Failure = Error;
    using Result  = cetl::variant<Success, Failure>;

    CETL_NODISCARD static Ptr make(const ClientContext& context, const Spec::Request& request);

    RawRpcClient(RawRpcClient&&)                 = delete;
    RawRpcClient(const RawRpcClient&)            = delete;
    RawRpcClient& operator=(RawRpcClient&&)      = delete;
    RawRpcClient& operator=(const RawRpcClient&) = delete;

    virtual ~RawRpcClient() = default;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    RawRpcClient() = default;

    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // RawRpcClient

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_RELAY_RAW_RPC_CLIENT_HPP_INCLUDED
