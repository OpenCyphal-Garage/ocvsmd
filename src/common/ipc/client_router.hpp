//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_CLIENT_ROUTER_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_CLIENT_ROUTER_HPP_INCLUDED

#include "channel.hpp"
#include "gateway.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "pipe/client_pipe.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <memory>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

/// Defines interface of the IPC client-side router.
///
class ClientRouter
{
public:
    using Ptr = std::shared_ptr<ClientRouter>;

    CETL_NODISCARD static Ptr make(cetl::pmr::memory_resource& memory, pipe::ClientPipe::Ptr client_pipe);

    // No copy/move.
    ClientRouter(const ClientRouter&)                = delete;
    ClientRouter(ClientRouter&&) noexcept            = delete;
    ClientRouter& operator=(const ClientRouter&)     = delete;
    ClientRouter& operator=(ClientRouter&&) noexcept = delete;

    virtual ~ClientRouter() = default;

    CETL_NODISCARD virtual sdk::ErrorCode              start()  = 0;
    CETL_NODISCARD virtual cetl::pmr::memory_resource& memory() = 0;

    template <typename Ch>
    CETL_NODISCARD Ch makeChannel(const cetl::string_view service_name = "")
    {
        const auto svc_desc = AnyChannel::getServiceDesc<typename Ch::Output>(service_name);
        return Ch{memory(), makeGateway(), svc_desc.id};
    }

protected:
    ClientRouter() = default;

    CETL_NODISCARD virtual detail::Gateway::Ptr makeGateway() = 0;

};  // ClientRouter

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_CLIENT_ROUTER_HPP_INCLUDED
