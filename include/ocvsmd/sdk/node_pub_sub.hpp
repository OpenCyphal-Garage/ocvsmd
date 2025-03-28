//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED
#define OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED

#include "execution.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <memory>

namespace ocvsmd
{
namespace sdk
{

/// Defines the interface for the Raw Messages Subscriber.
///
class RawSubscriber
{
public:
    /// Defines a smart pointer type for the interface.
    ///
    /// It's made "shared" b/c execution sender (see `receive` method) implicitly
    /// holds reference to its subscriber.
    ///
    using Ptr = std::shared_ptr<RawSubscriber>;

    virtual ~RawSubscriber() = default;

    // No copy/move semantics.
    RawSubscriber(RawSubscriber&&)                 = delete;
    RawSubscriber(const RawSubscriber&)            = delete;
    RawSubscriber& operator=(RawSubscriber&&)      = delete;
    RawSubscriber& operator=(const RawSubscriber&) = delete;

    /// Defines the result type of the raw subscriber message reception.
    ///
    /// On success, the result is a raw data buffer, its size, and extra metadata.
    /// On failure, the result is an SDK error.
    ///
    struct Receive final
    {
        struct Success
        {
            std::size_t                   size;
            std::unique_ptr<cetl::byte[]> data;  // NOLINT(*-avoid-c-arrays)
            CyphalPriority                priority;
            cetl::optional<CyphalNodeId>  publisher_node_id;
        };
        using Failure = Error;
        using Result  = cetl::variant<Success, Failure>;
    };
    /// Receives the next raw message from this subscriber.
    ///
    /// The server-side (the daemon) will forward the observed raw data on the corresponding Cyphal network subscriber.
    /// The raw data is forwarded as is, without any interpretation or validation.
    ///
    /// Note, only one `receive` operation can be active at a time (per subscriber).
    /// In the case of multiple "concurrent" operations, only the last one will receive the result.
    /// Any previous still existing operations will be "stalled" and never complete.
    /// Also, to not miss any new message, user should immediately initiate
    /// a new `receive` operation after getting the success result of the previous one.
    ///
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<Receive::Result>::Ptr receive() = 0;

protected:
    RawSubscriber() = default;

};  // RawSubscriber

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED
