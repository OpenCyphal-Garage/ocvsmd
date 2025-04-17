//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_RPC_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_NODE_RPC_CLIENT_HPP_INCLUDED

#include "defines.hpp"
#include "execution.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <chrono>
#include <memory>

namespace ocvsmd
{
namespace sdk
{

/// Defines the interface of RPC Client to a Cyphal network node.
///
class NodeRpcClient
{
public:
    /// Defines a smart pointer type for the interface.
    ///
    /// It's made "shared" b/c execution sender (see `request` method) implicitly
    /// holds reference to its client.
    ///
    using Ptr = std::shared_ptr<NodeRpcClient>;

    virtual ~NodeRpcClient() = default;

    // No copy/move semantics.
    NodeRpcClient(NodeRpcClient&&)                 = delete;
    NodeRpcClient(const NodeRpcClient&)            = delete;
    NodeRpcClient& operator=(NodeRpcClient&&)      = delete;
    NodeRpcClient& operator=(const NodeRpcClient&) = delete;

    /// Defines the result type of the RPC client raw response.
    ///
    /// On success, the result is a raw data buffer, its size, and extra metadata.
    /// On failure, the result is an SDK error.
    ///
    struct RawResponse final
    {
        struct Success
        {
            OwnedMutablePayload payload;
            CyphalPriority      priority;
            CyphalNodeId        server_node_id;
        };
        using Failure = Error;
        using Result  = cetl::variant<Success, Failure>;
    };
    /// Sends raw RPC request.
    ///
    /// The client-side (the SDK) will forward the raw data to the corresponding Cyphal network service client
    /// on the server-side (the daemon). The raw data is forwarded as is, without any interpretation or validation.
    ///
    /// Note, only one request can be active at a time (per rpc client).
    /// In the case of multiple "concurrent" operations, only the last one will report the response result.
    /// Any previous still existing operations will be "stalled" and never complete.
    ///
    /// @param raw_payload The raw request data to be sent.
    /// @param request_timeout The maximum time to keep the raw request as valid in the Cyphal network.
    /// @param response_timeout The maximum time to wait for reply from the Cyphal node.
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<RawResponse::Result>::Ptr rawRequest(OwnedMutablePayload&&           raw_payload,
                                                          const std::chrono::microseconds request_timeout,
                                                          const std::chrono::microseconds response_timeout) = 0;

    /// Sets priority for request to be issued by this client.
    ///
    /// The next and following `request` operations will use this priority.
    ///
    virtual OptError setPriority(const CyphalPriority priority) = 0;

protected:
    NodeRpcClient() = default;

};  // NodeRpcClient

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_RPC_CLIENT_HPP_INCLUDED
