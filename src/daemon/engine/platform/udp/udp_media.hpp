//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_PLATFORM_UDP_MEDIA_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_PLATFORM_UDP_MEDIA_HPP_INCLUDED

#include "udp_sockets.hpp"

#include <udpard.h>

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>
#include <libcyphal/executor.hpp>
#include <libcyphal/transport/udp/media.hpp>
#include <libcyphal/transport/udp/tx_rx_sockets.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace platform
{
namespace udp
{

class UdpMedia final : public libcyphal::transport::udp::IMedia
{
public:
    UdpMedia(cetl::pmr::memory_resource& general_mr,
             libcyphal::IExecutor&       executor,
             const cetl::string_view     iface_address,
             cetl::pmr::memory_resource& tx_mr)
        : general_mr_{general_mr}
        , executor_{executor}
        , iface_address_{iface_address.data(), iface_address.size()}
        , iface_mtu_{UDPARD_MTU_DEFAULT}
        , tx_mr_{tx_mr}
    {
    }

    ~UdpMedia() = default;

    UdpMedia(const UdpMedia&)                = delete;
    UdpMedia& operator=(const UdpMedia&)     = delete;
    UdpMedia* operator=(UdpMedia&&) noexcept = delete;

    UdpMedia(UdpMedia&& other) noexcept
        : general_mr_{other.general_mr_}
        , executor_{other.executor_}
        , iface_address_{std::move(other.iface_address_)}
        , iface_mtu_{other.iface_mtu_}
        , tx_mr_{other.tx_mr_}
    {
    }

    void setAddress(const cetl::string_view iface_address)
    {
        iface_address_ = std::string{iface_address.data(), iface_address.size()};
    }

    void setMtu(const std::size_t mtu)
    {
        iface_mtu_ = mtu;
    }

private:
    // MARK: - IMedia

    MakeTxSocketResult::Type makeTxSocket() override
    {
        return UdpTxSocket::make(general_mr_, executor_, iface_address_, iface_mtu_);
    }

    MakeRxSocketResult::Type makeRxSocket(const libcyphal::transport::udp::IpEndpoint& multicast_endpoint) override
    {
        return UdpRxSocket::make(general_mr_, executor_, iface_address_, multicast_endpoint);
    }

    cetl::pmr::memory_resource& getTxMemoryResource() override
    {
        return tx_mr_;
    }

    // MARK: Data members:

    cetl::pmr::memory_resource& general_mr_;
    libcyphal::IExecutor&       executor_;
    std::string                 iface_address_;
    std::size_t                 iface_mtu_;
    cetl::pmr::memory_resource& tx_mr_;

};  // UdpMedia

// MARK: -

struct UdpMediaCollection
{
    UdpMediaCollection(cetl::pmr::memory_resource& general_mr,
                       libcyphal::IExecutor&       executor,
                       cetl::pmr::memory_resource& tx_mr)
        : media_array_{{//
                        {general_mr, executor, "", tx_mr},
                        {general_mr, executor, "", tx_mr},
                        {general_mr, executor, "", tx_mr}}}
    {
    }

    void parse(const cetl::string_view iface_addresses, const cetl::optional<std::size_t> opt_iface_mtu)
    {
        // Split addresses by commas.
        //
        std::size_t index = 0;
        std::size_t curr  = 0;
        while ((curr != cetl::string_view::npos) && (index < MaxUdpMedia))
        {
            const auto next          = iface_addresses.find(',', curr);
            const auto iface_address = iface_addresses.substr(curr, next - curr);
            if (!iface_address.empty())
            {
                auto& media = media_array_[index];  // NOLINT

                media.setAddress(iface_address);
                if (opt_iface_mtu)
                {
                    media.setMtu(*opt_iface_mtu);
                }
                index++;
            }

            curr = std::max(next + 1, next);  // `+1` to skip the comma
        }

        media_ifaces_ = {};
        for (std::size_t i = 0; i < index; i++)
        {
            media_ifaces_[i] = &media_array_[i];  // NOLINT
        }
    }

    cetl::span<libcyphal::transport::udp::IMedia*> span()
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
    static constexpr std::size_t MaxUdpMedia = 3;

    std::array<UdpMedia, MaxUdpMedia>                           media_array_;
    std::array<libcyphal::transport::udp::IMedia*, MaxUdpMedia> media_ifaces_{};

};  // UdpMediaCollection

}  // namespace udp
}  // namespace platform
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_PLATFORM_UDP_MEDIA_HPP_INCLUDED
