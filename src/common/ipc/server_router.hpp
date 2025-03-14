//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IPC_SERVER_ROUTER_HPP_INCLUDED
#define OCVSMD_COMMON_IPC_SERVER_ROUTER_HPP_INCLUDED

#include "channel.hpp"
#include "gateway.hpp"
#include "ipc_types.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "pipe/server_pipe.hpp"

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <functional>
#include <memory>

namespace ocvsmd
{
namespace common
{
namespace ipc
{

/// Defines interface of the IPC server-side router.
///
class ServerRouter
{
public:
    using Ptr = std::unique_ptr<ServerRouter>;

    CETL_NODISCARD static Ptr make(cetl::pmr::memory_resource& memory, pipe::ServerPipe::Ptr server_pipe);

    // No copy/move.
    ServerRouter(const ServerRouter&)                = delete;
    ServerRouter(ServerRouter&&) noexcept            = delete;
    ServerRouter& operator=(const ServerRouter&)     = delete;
    ServerRouter& operator=(ServerRouter&&) noexcept = delete;

    virtual ~ServerRouter() = default;

    CETL_NODISCARD virtual sdk::OptErrorCode           start()  = 0;
    CETL_NODISCARD virtual cetl::pmr::memory_resource& memory() = 0;

    template <typename Ch>
    using NewChannelHandler = std::function<void(Ch&& new_channel, const typename Ch::Input& input)>;

    template <typename Ch>
    void registerChannel(const cetl::string_view service_name, NewChannelHandler<Ch> handler)
    {
        CETL_DEBUG_ASSERT(handler, "");

        const auto svc_desc = AnyChannel::getServiceDesc<typename Ch::Input>(service_name);

        registerChannelFactory(  //
            svc_desc,
            [this, svc_id = svc_desc.id, new_ch_handler = std::move(handler)](detail::Gateway::Ptr gateway,
                                                                              const Payload        payload) {
                typename Ch::Input input{&memory()};
                if (tryDeserializePayload(payload, input))
                {
                    new_ch_handler(Ch{memory(), gateway, svc_id}, input);
                }
            });
    }

protected:
    using TypeErasedChannelFactory = std::function<void(detail::Gateway::Ptr gateway, const Payload payload)>;

    ServerRouter() = default;

    virtual void registerChannelFactory(const detail::ServiceDesc service_desc,
                                        TypeErasedChannelFactory  channel_factory) = 0;

};  // ServerRouter

}  // namespace ipc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IPC_SERVER_ROUTER_HPP_INCLUDED
