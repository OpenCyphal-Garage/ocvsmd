//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_PLATFORM_CAN_MEDIA_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_PLATFORM_CAN_MEDIA_HPP_INCLUDED

#include "ocvsmd/platform/posix_executor_extension.hpp"
#include "ocvsmd/platform/posix_platform_error.hpp"
#include "socketcan.h"

#include <canard.h>
#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>
#include <cetl/rtti.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/transport/can/media.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/transport/media_payload.hpp>
#include <libcyphal/types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace platform
{
namespace can
{

class CanMedia final : public libcyphal::transport::can::IMedia
{
public:
    CETL_NODISCARD static cetl::variant<CanMedia, libcyphal::transport::PlatformError> make(
        cetl::pmr::memory_resource& general_mr,
        libcyphal::IExecutor&       executor,
        const cetl::string_view     iface_address_sv,
        const std::size_t           iface_mtu,
        cetl::pmr::memory_resource& tx_mr)
    {
        std::string iface_address{iface_address_sv.data(), iface_address_sv.size()};

        const SocketCANFD socket_can_rx_fd = ::socketcanOpen(iface_address.c_str(), iface_mtu);
        if (socket_can_rx_fd < 0)
        {
            return libcyphal::transport::PlatformError{ocvsmd::platform::PosixPlatformError{-socket_can_rx_fd}};
        }

        // We gonna register separate callbacks for rx & tx (aka pop & push),
        // so at executor (especially in case of the "epoll" one) we need separate file descriptors.
        //
        const SocketCANFD socket_can_tx_fd = ::socketcanOpen(iface_address.c_str(), iface_mtu);
        if (socket_can_tx_fd < 0)
        {
            const int error_code = -socket_can_tx_fd;
            (void) ::close(socket_can_rx_fd);
            return libcyphal::transport::PlatformError{ocvsmd::platform::PosixPlatformError{error_code}};
        }

        return CanMedia{general_mr,
                        executor,
                        socket_can_rx_fd,
                        socket_can_tx_fd,
                        std::move(iface_address),
                        iface_mtu,
                        tx_mr};
    }

    ~CanMedia()
    {
        if (socket_can_rx_fd_ >= 0)
        {
            (void) ::close(socket_can_rx_fd_);
        }
        if (socket_can_tx_fd_ >= 0)
        {
            (void) ::close(socket_can_tx_fd_);
        }
    }

    CanMedia(const CanMedia&)                = delete;
    CanMedia& operator=(const CanMedia&)     = delete;
    CanMedia* operator=(CanMedia&&) noexcept = delete;

    CanMedia(CanMedia&& other) noexcept
        : general_mr_{other.general_mr_}
        , executor_{other.executor_}
        , socket_can_rx_fd_{std::exchange(other.socket_can_rx_fd_, -1)}
        , socket_can_tx_fd_{std::exchange(other.socket_can_tx_fd_, -1)}
        , iface_address_{std::move(other.iface_address_)}
        , iface_mtu_{other.iface_mtu_}
        , tx_mr_{other.tx_mr_}
    {
    }

    void tryReopen()
    {
        if (socket_can_rx_fd_ >= 0)
        {
            (void) ::close(socket_can_rx_fd_);
            socket_can_rx_fd_ = -1;
        }
        if (socket_can_tx_fd_ >= 0)
        {
            (void) ::close(socket_can_tx_fd_);
            socket_can_tx_fd_ = -1;
        }

        const SocketCANFD socket_can_rx_fd = ::socketcanOpen(iface_address_.c_str(), iface_mtu_);
        if (socket_can_rx_fd >= 0)
        {
            socket_can_rx_fd_ = socket_can_rx_fd;
        }

        const SocketCANFD socket_can_tx_fd = ::socketcanOpen(iface_address_.c_str(), iface_mtu_);
        if (socket_can_tx_fd >= 0)
        {
            socket_can_tx_fd_ = socket_can_tx_fd;
        }
    }

private:
    using Filter  = libcyphal::transport::can::Filter;
    using Filters = libcyphal::transport::can::Filters;

    CanMedia(cetl::pmr::memory_resource& general_mr,
             libcyphal::IExecutor&       executor,
             const SocketCANFD           socket_can_rx_fd,
             const SocketCANFD           socket_can_tx_fd,
             std::string                 iface_address,
             const std::size_t           iface_mtu,
             cetl::pmr::memory_resource& tx_mr)
        : general_mr_{general_mr}
        , executor_{executor}
        , socket_can_rx_fd_{socket_can_rx_fd}
        , socket_can_tx_fd_{socket_can_tx_fd}
        , iface_address_{std::move(iface_address)}
        , iface_mtu_{iface_mtu}
        , tx_mr_{tx_mr}
    {
    }

    CETL_NODISCARD libcyphal::IExecutor::Callback::Any registerAwaitableCallback(
        libcyphal::IExecutor::Callback::Function&&                         function,
        const ocvsmd::platform::IPosixExecutorExtension::Trigger::Variant& trigger) const
    {
        auto* const posix_executor_ext = cetl::rtti_cast<ocvsmd::platform::IPosixExecutorExtension*>(&executor_);
        if (nullptr == posix_executor_ext)
        {
            return {};
        }

        return posix_executor_ext->registerAwaitableCallback(std::move(function), trigger);
    }

    // MARK: - IMedia

    std::size_t getMtu() const noexcept override
    {
        return iface_mtu_;
    }

    cetl::optional<libcyphal::transport::MediaFailure> setFilters(const Filters filters) noexcept override
    {
        std::vector<CanardFilter> can_filters;
        can_filters.reserve(filters.size());
        std::transform(filters.begin(), filters.end(), std::back_inserter(can_filters), [](const Filter filter) {
            //
            return CanardFilter{filter.id, filter.mask};
        });

        const std::int16_t result = ::socketcanFilter(socket_can_rx_fd_, can_filters.size(), can_filters.data());
        if (result < 0)
        {
            return libcyphal::transport::PlatformError{ocvsmd::platform::PosixPlatformError{-result}};
        }
        return cetl::nullopt;
    }

    PushResult::Type push(const libcyphal::TimePoint /* deadline */,
                          const libcyphal::transport::can::CanId can_id,
                          libcyphal::transport::MediaPayload&    payload) noexcept override
    {
        const CanardFrame  canard_frame{can_id,
                                        {payload.getSpan().size(), static_cast<const void*>(payload.getSpan().data())}};
        const std::int16_t result = ::socketcanPush(socket_can_tx_fd_, &canard_frame, 0);
        if (result < 0)
        {
            return libcyphal::transport::PlatformError{ocvsmd::platform::PosixPlatformError{-result}};
        }

        const bool is_accepted = result > 0;
        if (is_accepted)
        {
            // Payload is not needed anymore, so return memory asap.
            payload.reset();
        }

        return PushResult::Success{is_accepted};
    }

    CETL_NODISCARD PopResult::Type pop(const cetl::span<cetl::byte> payload_buffer) noexcept override
    {
        CanardFrame canard_frame{};
        bool        is_loopback{false};

        const std::int16_t result = ::socketcanPop(socket_can_rx_fd_,
                                                   &canard_frame,
                                                   nullptr,
                                                   payload_buffer.size(),
                                                   payload_buffer.data(),
                                                   0,
                                                   &is_loopback);
        if (result < 0)
        {
            return libcyphal::transport::PlatformError{ocvsmd::platform::PosixPlatformError{-result}};
        }
        if (result == 0)
        {
            return cetl::nullopt;
        }

        return PopResult::Metadata{executor_.now(), canard_frame.extended_can_id, canard_frame.payload.size};
    }

    CETL_NODISCARD libcyphal::IExecutor::Callback::Any registerPushCallback(
        libcyphal::IExecutor::Callback::Function&& function) override
    {
        using WritableTrigger = ocvsmd::platform::IPosixExecutorExtension::Trigger::Writable;
        return registerAwaitableCallback(std::move(function), WritableTrigger{socket_can_tx_fd_});
    }

    CETL_NODISCARD libcyphal::IExecutor::Callback::Any registerPopCallback(
        libcyphal::IExecutor::Callback::Function&& function) override
    {
        using ReadableTrigger = ocvsmd::platform::IPosixExecutorExtension::Trigger::Readable;
        return registerAwaitableCallback(std::move(function), ReadableTrigger{socket_can_rx_fd_});
    }

    cetl::pmr::memory_resource& getTxMemoryResource() override
    {
        return tx_mr_;
    }

    // MARK: Data members:

    cetl::pmr::memory_resource& general_mr_;
    libcyphal::IExecutor&       executor_;
    SocketCANFD                 socket_can_rx_fd_;
    SocketCANFD                 socket_can_tx_fd_;
    std::string                 iface_address_;
    std::size_t                 iface_mtu_;
    cetl::pmr::memory_resource& tx_mr_;

};  // CanMedia

// MARK: -

struct CanMediaCollection
{
    CanMediaCollection(cetl::pmr::memory_resource& general_mr,
                       libcyphal::IExecutor&       executor,
                       cetl::pmr::memory_resource& tx_mr)
        : general_mr_{general_mr}
        , executor_{executor}
        , media_array_{{cetl::nullopt, cetl::nullopt, cetl::nullopt}}
        , tx_mr_{tx_mr}
    {
    }

    void parse(const cetl::string_view iface_addresses, const std::size_t iface_mtu)
    {
        // Reset the collection.
        for (std::size_t i = 0; i < MaxCanMedia; i++)
        {
            media_array_[i].reset();     // NOLINT
            media_ifaces_[i] = nullptr;  // NOLINT
        }

        // Split addresses by commas.
        //
        std::size_t index = 0;
        std::size_t curr  = 0;
        while ((curr != cetl::string_view::npos) && (index < MaxCanMedia))
        {
            const auto next          = iface_addresses.find(',', curr);
            const auto iface_address = iface_addresses.substr(curr, next - curr);
            if (!iface_address.empty())
            {
                auto maybe_media = CanMedia::make(general_mr_, executor_, iface_address, iface_mtu, tx_mr_);
                if (auto* const media_ptr = cetl::get_if<CanMedia>(&maybe_media))
                {
                    media_array_[index].emplace(std::move(*media_ptr));     // NOLINT
                    media_ifaces_[index] = &(media_array_[index].value());  // NOLINT
                    index++;
                }
            }

            curr = std::max(next + 1, next);  // `+1` to skip the comma
        }
    }

    cetl::span<libcyphal::transport::can::IMedia*> span()
    {
        return {media_ifaces_.data(), media_ifaces_.size()};
    }

    std::size_t count() const
    {
        return std::count_if(media_ifaces_.cbegin(), media_ifaces_.cend(), [](const auto* iface) {
            //
            return iface != nullptr;
        });
    }

private:
    static constexpr std::size_t MaxCanMedia = 3;

    cetl::pmr::memory_resource&                                 general_mr_;
    libcyphal::IExecutor&                                       executor_;
    std::array<cetl::optional<CanMedia>, MaxCanMedia>           media_array_;
    std::array<libcyphal::transport::can::IMedia*, MaxCanMedia> media_ifaces_{};
    cetl::pmr::memory_resource&                                 tx_mr_;

};  // CanMediaCollection

}  // namespace can
}  // namespace platform
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_PLATFORM_CAN_MEDIA_HPP_INCLUDED
