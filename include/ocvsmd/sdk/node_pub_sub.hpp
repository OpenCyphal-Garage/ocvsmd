//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED
#define OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED

#include "execution.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <chrono>
#include <memory>

namespace ocvsmd
{
namespace sdk
{

/// Defines the interface for the Raw Messages Publisher.
///
class RawPublisher
{
public:
    /// Defines a smart pointer type for the interface.
    ///
    /// It's made "shared" b/c execution sender (see `publish` method) implicitly
    /// holds reference to its publisher.
    ///
    using Ptr = std::shared_ptr<RawPublisher>;

    virtual ~RawPublisher() = default;

    // No copy/move semantics.
    RawPublisher(RawPublisher&&)                 = delete;
    RawPublisher(const RawPublisher&)            = delete;
    RawPublisher& operator=(RawPublisher&&)      = delete;
    RawPublisher& operator=(const RawPublisher&) = delete;

    /// Publishes the next raw message using this publisher.
    ///
    /// The client-side (the SDK) will forward the raw data to the corresponding Cyphal network publisher
    /// on the server-side (the daemon). The raw data is forwarded as is, without any interpretation or validation.
    ///
    /// Note, only one operation can be active at a time (per publisher).
    /// In the case of multiple "concurrent" operations, only the last one will report the publishing result.
    /// Any previous still existing operations will be "stalled" and never complete.
    ///
    /// @param raw_msg The raw message data to publish.
    /// @param timeout The maximum time to keep the published raw message as valid in the Cyphal network.
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<OptError>::Ptr publish(const cetl::span<const cetl::byte> raw_msg,
                                            const std::chrono::microseconds    timeout) = 0;

    /// Gets the current priority assigned to this raw publisher.
    ///
    virtual CyphalPriority getPriority() const = 0;

    /// Sets priority for raw messages to be issued by this raw publisher.
    ///
    /// The next and following `publish` operations will use this priority.
    ///
    virtual OptError setPriority(const CyphalPriority priority);

protected:
    RawPublisher() = default;

};  // RawPublisher

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
