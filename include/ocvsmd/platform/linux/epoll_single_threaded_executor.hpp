//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_PLATFORM_LINUX_EPOLL_SINGLE_THREADED_EXECUTOR_HPP_INCLUDED
#define OCVSMD_PLATFORM_LINUX_EPOLL_SINGLE_THREADED_EXECUTOR_HPP_INCLUDED

#include "ocvsmd/platform/posix_executor_extension.hpp"
#include "ocvsmd/platform/posix_platform_error.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/rtti.hpp>
#include <cetl/visit_helpers.hpp>
#include <libcyphal/errors.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/platform/single_threaded_executor.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/types.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace ocvsmd
{
namespace platform
{
namespace Linux
{

/// @brief Defines Linux platform-specific single-threaded executor based on `epoll` mechanism.
///
class EpollSingleThreadedExecutor final : public libcyphal::platform::SingleThreadedExecutor,
                                          public IPosixExecutorExtension
{
public:
    EpollSingleThreadedExecutor()
        : epollfd_{::epoll_create1(0)}
        , total_awaitables_{0}
    {
    }

    EpollSingleThreadedExecutor(const EpollSingleThreadedExecutor&)                = delete;
    EpollSingleThreadedExecutor(EpollSingleThreadedExecutor&&) noexcept            = delete;
    EpollSingleThreadedExecutor& operator=(const EpollSingleThreadedExecutor&)     = delete;
    EpollSingleThreadedExecutor& operator=(EpollSingleThreadedExecutor&&) noexcept = delete;

    ~EpollSingleThreadedExecutor() override
    {
        if (epollfd_ >= 0)
        {
            ::close(epollfd_);
        }
    }

    CETL_NODISCARD cetl::optional<PollFailure> pollAwaitableResourcesFor(
        const cetl::optional<libcyphal::Duration> timeout) const override
    {
        CETL_DEBUG_ASSERT((total_awaitables_ > 0) || timeout,
                          "Infinite timeout without awaitables means that we will sleep forever.");

        if (total_awaitables_ == 0)
        {
            if (!timeout)
            {
                return libcyphal::ArgumentError{};
            }

            std::this_thread::sleep_for(*timeout);
            return cetl::nullopt;
        }

        // Make sure that timeout is within the range of `::epoll_wait()`'s `int` timeout parameter.
        // Any possible negative timeout will be treated as zero (return immediately from the `::epoll_wait`).
        //
        int clamped_timeout_ms = -1;  // "infinite" timeout
        if (timeout)
        {
            using PollDuration = std::chrono::milliseconds;

            clamped_timeout_ms = static_cast<int>(  //
                std::max(static_cast<PollDuration::rep>(0),
                         std::min(std::chrono::duration_cast<PollDuration>(*timeout).count(),
                                  static_cast<PollDuration::rep>(std::numeric_limits<int>::max()))));
        }

        std::array<epoll_event, MaxEpollEvents> evs{};
        const int epoll_result = ::epoll_wait(epollfd_, evs.data(), evs.size(), clamped_timeout_ms);
        if (epoll_result < 0)
        {
            const auto err = errno;
            if (err == EINTR)
            {
                // Normally, we would just retry a system call (`::epoll_wait`),
                // but we need updated timeout (from the main loop).
                return cetl::nullopt;
            }
            return libcyphal::transport::PlatformError{PosixPlatformError{err}};
        }
        if (epoll_result == 0)
        {
            return cetl::nullopt;
        }
        const auto epoll_nfds = static_cast<std::size_t>(epoll_result);

        const auto now_time = now();
        for (std::size_t index = 0; index < epoll_nfds; ++index)
        {
            const epoll_event& ev = evs[index];
            if (auto* const cb_interface = static_cast<AwaitableNode*>(ev.data.ptr))
            {
                cb_interface->schedule(Callback::Schedule::Once{now_time});
            }
        }

        return cetl::nullopt;
    }

protected:
    // MARK: - IPosixExecutorExtension

    CETL_NODISCARD Callback::Any registerAwaitableCallback(Callback::Function&&    function,
                                                           const Trigger::Variant& trigger) override
    {
        AwaitableNode new_cb_node{*this, std::move(function)};

        cetl::visit(  //
            cetl::make_overloaded(
                [&new_cb_node](const Trigger::Readable& readable) {
                    //
                    new_cb_node.setup(readable.fd, EPOLLIN);
                },
                [&new_cb_node](const Trigger::Writable& writable) {
                    //
                    new_cb_node.setup(writable.fd, EPOLLOUT);
                }),
            trigger);

        insertCallbackNode(new_cb_node);
        return {std::move(new_cb_node)};
    }

    // MARK: - RTTI

    CETL_NODISCARD void* _cast_(const cetl::type_id& id) & noexcept override
    {
        if (id == IPosixExecutorExtension::_get_type_id_())
        {
            return static_cast<IPosixExecutorExtension*>(this);
        }
        return Base::_cast_(id);
    }
    CETL_NODISCARD const void* _cast_(const cetl::type_id& id) const& noexcept override
    {
        if (id == IPosixExecutorExtension::_get_type_id_())
        {
            return static_cast<const IPosixExecutorExtension*>(this);
        }
        return Base::_cast_(id);
    }

private:
    using Base = SingleThreadedExecutor;
    using Self = EpollSingleThreadedExecutor;

    /// No Sonar cpp:S4963 b/c `AwaitableNode` supports move operation.
    ///
    class AwaitableNode final : public CallbackNode  // NOSONAR cpp:S4963
    {
    public:
        AwaitableNode(Self& executor, Callback::Function&& function)
            : CallbackNode{executor, std::move(function)}
            , fd_{-1}
            , events_{0}
        {
        }

        ~AwaitableNode() override
        {
            if (fd_ >= 0)
            {
                ::epoll_ctl(getExecutor().epollfd_, EPOLL_CTL_DEL, fd_, nullptr);
                getExecutor().total_awaitables_--;
            }
        }

        AwaitableNode(AwaitableNode&& other) noexcept
            : CallbackNode(std::move(other))
            , fd_{std::exchange(other.fd_, -1)}
            , events_{std::exchange(other.events_, 0)}
        {
            if (fd_ >= 0)
            {
                ::epoll_event ev{events_, {this}};
                ::epoll_ctl(getExecutor().epollfd_, EPOLL_CTL_MOD, fd_, &ev);
            }
        }

        AwaitableNode(const AwaitableNode&)                      = delete;
        AwaitableNode& operator=(const AwaitableNode&)           = delete;
        AwaitableNode& operator=(AwaitableNode&& other) noexcept = delete;

        int fd() const noexcept
        {
            return fd_;
        }

        std::uint32_t events() const noexcept
        {
            return events_;
        }

        void setup(const int fd, const std::uint32_t events) noexcept
        {
            CETL_DEBUG_ASSERT(fd >= 0, "");
            CETL_DEBUG_ASSERT(events != 0, "");

            fd_     = fd;
            events_ = events;

            getExecutor().total_awaitables_++;
            ::epoll_event ev{events_, {this}};
            ::epoll_ctl(getExecutor().epollfd_, EPOLL_CTL_ADD, fd_, &ev);
        }

    private:
        Self& getExecutor() noexcept
        {
            // No lint b/c we know for sure that the executor is of type `Self`.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            return static_cast<Self&>(executor());
        }

        // MARK: Data members:

        int           fd_;
        std::uint32_t events_;

    };  // AwaitableNode

    // MARK: - Data members:

    static constexpr int MaxEpollEvents = 16;

    int         epollfd_;
    std::size_t total_awaitables_;

};  // EpollSingleThreadedExecutor

}  // namespace Linux
}  // namespace platform
}  // namespace ocvsmd

#endif  // OCVSMD_PLATFORM_LINUX_EPOLL_SINGLE_THREADED_EXECUTOR_HPP_INCLUDED
