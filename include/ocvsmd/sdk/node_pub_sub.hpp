//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED
#define OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED

#include "defines.hpp"
#include "execution.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <chrono>
#include <memory>

namespace ocvsmd
{
namespace sdk
{

/// Defines the interface of Messages Publisher.
///
class Publisher
{
public:
    /// Defines a smart pointer type for the interface.
    ///
    /// It's made "shared" b/c execution sender (see `publish` method) implicitly
    /// holds reference to its publisher.
    ///
    using Ptr = std::shared_ptr<Publisher>;

    virtual ~Publisher() = default;

    // No copy/move semantics.
    Publisher(Publisher&&)                 = delete;
    Publisher(const Publisher&)            = delete;
    Publisher& operator=(Publisher&&)      = delete;
    Publisher& operator=(const Publisher&) = delete;

    /// Publishes the next raw message using this publisher.
    ///
    /// The client-side (the SDK) will forward the raw data to the corresponding Cyphal network publisher
    /// on the server-side (the daemon). The raw data is forwarded as is, without any interpretation or validation.
    ///
    /// Note, only one operation can be active at a time (per publisher).
    /// In the case of multiple "concurrent" operations, only the last one will report the publishing result.
    /// Any previous still existing operations will be "stalled" and never complete.
    ///
    /// @param raw_payload The raw message data to publish.
    /// @param timeout The maximum time to keep the published raw message as valid in the Cyphal network.
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<OptError>::Ptr rawPublish(OwnMutablePayload&&             raw_payload,
                                               const std::chrono::microseconds timeout) = 0;

    /// Sets priority for messages to be issued by this publisher.
    ///
    /// The next and following `publish` operations will use this priority.
    ///
    virtual OptError setPriority(const CyphalPriority priority) = 0;

    /// Publishes the next message using this publisher.
    ///
    /// The client-side (the SDK) will forward the serialized message to the corresponding Cyphal network publisher
    /// on the server-side (the daemon).
    ///
    /// Note, only one operation can be active at a time (per publisher).
    /// In the case of multiple "concurrent" operations, only the last one will report the publishing result.
    /// Any previous still existing operations will be "stalled" and never complete.
    ///
    /// @param message The message to publish.
    /// @param timeout The maximum time to keep the published message as valid in the Cyphal network.
    /// @return An execution sender which emits the async result of the operation.
    ///
    template <typename Message>
    SenderOf<OptError>::Ptr publish(const Message& message, const std::chrono::microseconds timeout)
    {
        return tryPerformOnSerialized(message, [this, timeout](auto raw_payload) {
            //
            return rawPublish(std::move(raw_payload), timeout);
        });
    }

protected:
    Publisher() = default;

private:
    template <typename Message, typename Action>
    CETL_NODISCARD static SenderOf<OptError>::Ptr tryPerformOnSerialized(const Message& msg, Action&& action)
    {
#if defined(__cpp_exceptions)
        try
        {
#endif
            // Try to serialize the message to raw payload buffer.
            //
            constexpr std::size_t BufferSize = Message::_traits_::SerializationBufferSizeBytes;
            // NOLINTNEXTLINE(*-avoid-c-arrays)
            OwnMutablePayload payload{BufferSize, std::make_unique<cetl::byte[]>(BufferSize)};
            //
            // No lint b/c of integration with Nunavut.
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            const auto result_size =
                serialize(msg, {reinterpret_cast<std::uint8_t*>(payload.data.get()), payload.size});
            if (result_size)
            {
                payload.size = result_size.value();
                return std::forward<Action>(action)(std::move(payload));
            }
            return just<OptError>(Error{Error::Code::InvalidArgument});

#if defined(__cpp_exceptions)
        } catch (const std::bad_alloc&)
        {
            return just<OptError>(Error{Error::Code::OutOfMemory});
        }
#endif
    }

};  // Publisher

/// Defines the interface of Messages Subscriber.
///
class Subscriber
{
public:
    /// Defines a smart pointer type for the interface.
    ///
    /// It's made "shared" b/c execution sender (see `receive` method) implicitly
    /// holds reference to its subscriber.
    ///
    using Ptr = std::shared_ptr<Subscriber>;

    virtual ~Subscriber() = default;

    // No copy/move semantics.
    Subscriber(Subscriber&&)                 = delete;
    Subscriber(const Subscriber&)            = delete;
    Subscriber& operator=(Subscriber&&)      = delete;
    Subscriber& operator=(const Subscriber&) = delete;

    /// Defines the result type of the subscriber raw message reception.
    ///
    /// On success, the result is a raw data buffer, its size, and extra metadata.
    /// On failure, the result is an SDK error.
    ///
    struct RawReceive final
    {
        struct Success
        {
            OwnMutablePayload            payload;
            CyphalPriority               priority;
            cetl::optional<CyphalNodeId> publisher_node_id;
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
    virtual SenderOf<RawReceive::Result>::Ptr rawReceive() = 0;

    /// Defines the result type of the subscriber message reception.
    ///
    /// On success, the result is a deserialized message, and its extra metadata.
    /// On failure, the result is an SDK error.
    ///
    struct Receive final
    {
        template <typename Message>
        struct Success
        {
            Message                      message;
            CyphalPriority               priority;
            cetl::optional<CyphalNodeId> publisher_node_id;
        };

        using Failure = Error;

        template <typename Message>
        using Result = cetl::variant<Success<Message>, Failure>;
    };
    /// Receives the next message from this subscriber.
    ///
    /// The server-side (the daemon) will forward the observed raw data on the corresponding Cyphal network subscriber.
    /// The received raw data is then deserialized into the strong-typed message.
    ///
    /// Note, only one `receive` operation can be active at a time (per subscriber).
    /// In the case of multiple "concurrent" operations, only the last one will receive the result.
    /// Any previous still existing operations will be "stalled" and never complete.
    /// Also, to not miss any new message, user should immediately initiate
    /// a new `receive` operation after getting the success result of the previous one.
    ///
    /// @return An execution sender which emits the async result of the operation.
    ///
    template <typename Message>
    typename SenderOf<Receive::Result<Message>>::Ptr receive(cetl::pmr::memory_resource& memory)
    {
        using ResultMsg = Receive::Result<Message>;

        return then<ResultMsg, RawReceive::Result>(rawReceive(), [&memory](auto raw_result) -> ResultMsg {
            //
            if (const auto* const failure = cetl::get_if<RawReceive::Failure>(&raw_result))
            {
                return *failure;
            }
            auto raw_msg = cetl::get<RawReceive::Success>(std::move(raw_result));

            // No lint b/c of integration with Nunavut.
            // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
            const auto* const raw_payload = reinterpret_cast<const std::uint8_t*>(raw_msg.payload.data.get());
            Message           message{&memory};
            const auto        deser_result = deserialize(message, {raw_payload, raw_msg.payload.size});
            if (!deser_result)
            {
                // Invalid message payload.
                return Error{Error::Code::InvalidArgument};
            }

            return Receive::Success<Message>{
                std::move(message),
                raw_msg.priority,
                raw_msg.publisher_node_id,
            };
        });
    }

protected:
    Subscriber() = default;

};  // Subscriber

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED
