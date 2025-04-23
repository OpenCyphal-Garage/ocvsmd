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

    /// Defines the result type of the RPC client raw call.
    ///
    /// On success, the result is a raw response data buffer, its size, and extra metadata.
    /// On failure, the result is an SDK error.
    ///
    struct RawCall final
    {
        struct Success
        {
            OwnedMutablePayload raw_response;
            CyphalPriority      priority;
            CyphalNodeId        server_node_id;
        };
        using Failure = Error;
        using Result  = cetl::variant<Success, Failure>;
    };
    /// Sends raw RPC call.
    ///
    /// The client-side (the SDK) will forward the raw request data to the corresponding Cyphal network service client
    /// on the server-side (the daemon). The raw data is forwarded as is, without any interpretation or validation.
    ///
    /// Note, only one call can be active at a time (per rpc client).
    /// In the case of multiple "concurrent" operations, only the last one will report the response result.
    /// Any previous still existing operations will be "stalled" and never complete.
    ///
    /// @param raw_request The raw request data to be sent.
    /// @param request_timeout The maximum time to keep the raw request as valid in the Cyphal network.
    /// @param response_timeout The maximum time to wait for reply from the Cyphal node.
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<RawCall::Result>::Ptr rawCall(OwnedMutablePayload&&           raw_request,
                                                   const std::chrono::microseconds request_timeout,
                                                   const std::chrono::microseconds response_timeout) = 0;

    /// Sets priority for request to be issued by this client.
    ///
    /// The next and following `request` operations will use this priority.
    ///
    virtual OptError setPriority(const CyphalPriority priority) = 0;

    /// Defines the result type of the RPC client call.
    ///
    /// On success, the result is a deserialized response message, and extra metadata.
    /// On failure, the result is an SDK error.
    ///
    struct Call final
    {
        template <typename Response>
        struct Success
        {
            Response       response;
            CyphalPriority priority;
            CyphalNodeId   server_node_id;
        };

        using Failure = Error;

        template <typename Response>
        using Result = cetl::variant<Success<Response>, Failure>;
    };
    /// Sends RPC call.
    ///
    /// The client-side (the SDK) will forward the serialized request message to the corresponding Cyphal network
    /// service client on the server-side (the daemon).
    ///
    /// Note, only one call can be active at a time (per rpc client).
    /// In the case of multiple "concurrent" operations, only the last one will report the response result.
    /// Any previous still existing operations will be "stalled" and never complete.
    ///
    /// @param memory The memory resource to use for the response deserialization.
    /// @param request The request message to be sent.
    /// @param request_timeout The maximum time to keep the request as valid in the Cyphal network.
    /// @param response_timeout The maximum time to wait for reply from the Cyphal node.
    /// @return An execution sender which emits the async result of the operation.
    ///
    template <typename Response, typename Request>
    typename SenderOf<Call::Result<Response>>::Ptr call(cetl::pmr::memory_resource&     memory,
                                                        const Request&                  request,
                                                        const std::chrono::microseconds request_timeout,
                                                        const std::chrono::microseconds response_timeout)
    {
        using CallResult = Call::Result<Response>;

        auto raw_call_sender = detail::tryPerformOnSerialized<RawCall::Result>(  //
            request,
            [this, request_timeout, response_timeout](auto raw_request) {
                //
                return rawCall(std::move(raw_request), request_timeout, response_timeout);
            });

        return then<CallResult, RawCall::Result>(  //
            std::move(raw_call_sender),
            [&memory](auto raw_result) -> CallResult {
                //
                if (const auto* const failure = cetl::get_if<RawCall::Failure>(&raw_result))
                {
                    return *failure;
                }
                auto called = cetl::get<RawCall::Success>(std::move(raw_result));

                // No lint b/c of integration with Nunavut.
                // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
                const auto* const raw_payload = reinterpret_cast<const std::uint8_t*>(called.raw_response.data.get());
                Response          response{&memory};
                const auto        deser_result = deserialize(response, {raw_payload, called.raw_response.size});
                if (!deser_result)
                {
                    // Invalid message payload.
                    return Error{Error::Code::InvalidArgument};
                }

                return Call::Success<Response>{
                    std::move(response),
                    called.priority,
                    called.server_node_id,
                };
            });
    }

protected:
    NodeRpcClient() = default;

};  // NodeRpcClient

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_RPC_CLIENT_HPP_INCLUDED
