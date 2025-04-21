//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_EXECUTION_HPP_INCLUDED
#define OCVSMD_SDK_EXECUTION_HPP_INCLUDED

#include "ocvsmd/platform/defines.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <functional>
#include <memory>

namespace ocvsmd
{
namespace sdk
{

/// Internal implementation details.
/// Not supposed to be used directly by the users of the SDK.
///
namespace detail
{

// Defines various internal types for `sync_wait` implementation.
//
template <typename Result>
class StateOf;
//
template <typename Result>
class ReceiverOf final
{
public:
    explicit ReceiverOf(std::shared_ptr<StateOf<Result>> state)
        : state_{std::move(state)}
    {
    }

    template <typename... Args>
    void operator()(Args&&... args)
    {
        state_->complete(Result{std::forward<Args>(args)...});
    }

private:
    std::shared_ptr<StateOf<Result>> state_;

};  // ReceiverOf
//
template <typename Result>
class StateOf final : public std::enable_shared_from_this<StateOf<Result>>
{
public:
    bool completed() const
    {
        return maybe_result_.has_value();
    }

    Result get()
    {
        CETL_DEBUG_ASSERT(maybe_result_, "");
        return std::move(*maybe_result_);
    }

    ReceiverOf<Result> makeReceiver()
    {
        return ReceiverOf<Result>{this->shared_from_this()};
    }

    void complete(Result&& result)
    {
        if (!maybe_result_)
        {
            maybe_result_.emplace(std::move(result));
        }
    }

private:
    cetl::optional<Result> maybe_result_;

};  // StateOf

}  // namespace detail

/// Abstract interface of a result sender.
///
/// There is no (at least for now) support of async failures via C++ exceptions in the SDK.
/// So, if an async sender might fail, then the `Result` subtype
/// is expected to cover both success and failure cases (f.e. using `variant`).
///
template <typename Result_>
class SenderOf
{
public:
    /// Defines the shared pointer type for the interface.
    ///
    using Ptr = std::unique_ptr<SenderOf>;

    /// Defines the result type of the sender.
    ///
    using Result = Result_;

    // No copy/move semantics.
    SenderOf(SenderOf&&)                 = delete;
    SenderOf(const SenderOf&)            = delete;
    SenderOf& operator=(SenderOf&&)      = delete;
    SenderOf& operator=(const SenderOf&) = delete;

    virtual ~SenderOf() = default;

    /// Initiates an operation execution by submitting a given receiver to this sender.
    ///
    /// @tparam Receiver The type of the receiver functor. Should be callable with the result of the sender.
    /// @param receiver The receiver of the sender's result.
    ///                 Method "consumes" the receiver (no longer usable after this call).
    ///
    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    SenderOf() = default;

    /// Implementation extension point for the derived classes.
    ///
    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // SenderOf

/// A sender factory that returns a sender which completes immediately
/// by calling the receiver `operator()` with a `Result` type instance built using the given pack of arguments.
///
/// @param args The arguments to construct the result of the sender.
///             The result is constructed immediately (during this `just` call),
///             and later (on `SenderOf::submit`) is `move`-d to the receiver.
///
template <typename Result, typename... Args>
typename SenderOf<Result>::Ptr just(Args&&... args)
{
    // Private implementation of the sender.
    class JustSender final : public SenderOf<Result>
    {
    public:
        explicit JustSender(Args&&... args)
            : result_{std::forward<Args>(args)...}
        {
        }

    private:
        // SenderOf

        void submitImpl(std::function<void(Result&&)>&& receiver) override
        {
            std::move(receiver)(std::move(result_));
        }

        Result result_;

    };  // JustSender

    return std::make_unique<JustSender>(std::forward<Args>(args)...);
}

/// Initiates an operation execution by submitting a given receiver to the sender.
///
/// Submit "consumes" the receiver (no longer usable after this call).
///
template <typename Sender, typename Receiver>
void submit(Sender& sender, Receiver&& receiver)
{
    sender.submit(std::forward<Receiver>(receiver));
}

/// Initiates an operation execution by submitting a given receiver to the sender.
///
/// The submit "consumes" the receiver (no longer usable after this call).
///
template <typename Sender, typename Receiver>
void submit(std::unique_ptr<Sender>& sender_ptr, Receiver&& receiver)
{
    sender_ptr->submit(std::forward<Receiver>(receiver));
}

/// Algorithm that synchronously waits for the sender to emit result.
///
/// This algorithm "consumes" the sender, meaning that the sender is no longer usable after this call.
///
template <typename Result, typename Executor, typename Sender>
Result sync_wait(Executor& executor, Sender&& sender)
{
    auto state = std::make_shared<detail::StateOf<Result>>();

    submit(sender, state->makeReceiver());

    platform::waitPollingUntil(executor, [state] { return state->completed(); });

    return state->get();
}

template <typename Result, typename Executor, typename Sender, typename Duration>
Result sync_wait(Executor& executor, Sender&& sender, const Duration timeout)
{
    auto state = std::make_shared<detail::StateOf<Result>>();

    submit(sender, state->makeReceiver());

    auto timeout_cb = executor.registerCallback([state](const auto& arg) {
        //
        state->complete(Error{Error::Code::TimedOut});
    });

    const auto deadline = executor.now() + std::chrono::duration_cast<libcyphal::Duration>(timeout);
    timeout_cb.schedule(libcyphal::IExecutor::Callback::Schedule::Once{deadline});

    platform::waitPollingUntil(executor, [state] { return state->completed(); });

    return state->get();
}

template <typename Result, typename Input, typename Func>
typename SenderOf<Result>::Ptr then(typename SenderOf<Input>::Ptr input_sender, Func&& transform_func)
{
    // Private implementation of the sender.
    class ThenSender final : public SenderOf<Result>
    {
    public:
        ThenSender(typename SenderOf<Input>::Ptr input_sender, Func&& func)
            : input_sender_{std::move(input_sender)}
            , transform_func_{std::forward<Func>(func)}
        {
        }

    private:
        // SenderOf

        void submitImpl(std::function<void(Result&&)>&& receiver) override
        {
            input_sender_->submit([this, rc = std::move(receiver)](Input&& input) {
                //
                rc(transform_func_(std::move(input)));
            });
        }

        typename SenderOf<Input>::Ptr input_sender_;
        Func                          transform_func_;

    };  // ThenSender

    return std::make_unique<ThenSender>(std::move(input_sender), std::forward<Func>(transform_func));
}

/// Internal implementation details.
/// Not supposed to be used directly by the users of the SDK.
///
namespace detail
{

/// Helper function to serialize a message and perform an action on the serialized payload.
///
template <typename Result, typename Message, typename Action>
CETL_NODISCARD static typename SenderOf<Result>::Ptr tryPerformOnSerialized(const Message& msg, Action&& action)
{
#if defined(__cpp_exceptions)
    try
    {
#endif
        // Try to serialize the message to raw payload buffer.
        //
        constexpr std::size_t BufferSize = Message::_traits_::SerializationBufferSizeBytes;
        // NOLINTNEXTLINE(*-avoid-c-arrays)
        OwnedMutablePayload payload{BufferSize, std::make_unique<cetl::byte[]>(BufferSize)};
        //
        // No lint b/c of integration with Nunavut.
        // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
        auto* const payload_data = reinterpret_cast<std::uint8_t*>(payload.data.get());
        const auto  result_size  = serialize(msg, {payload_data, payload.size});
        if (result_size)
        {
            payload.size = result_size.value();
            return std::forward<Action>(action)(std::move(payload));
        }
        return just<Result>(Error{Error::Code::InvalidArgument});

#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&)
    {
        return just<Result>(Error{Error::Code::OutOfMemory});
    }
#endif
}

}  // namespace detail

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_EXECUTION_HPP_INCLUDED
