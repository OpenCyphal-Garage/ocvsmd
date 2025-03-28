//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_RELAY_CREATE_RAW_SUB_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_SVC_RELAY_CREATE_RAW_SUB_CLIENT_HPP_INCLUDED

#include "ipc/client_router.hpp"
#include "ocvsmd/sdk/defines.hpp"
#include "ocvsmd/sdk/node_pub_sub.hpp"
#include "svc/relay/create_raw_sub_spec.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <memory>
#include <utility>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{
namespace relay
{

/// Defines interface of the 'Relay: Create Raw Subscriber' service client.
///
class CreateRawSubClient
{
public:
    using Ptr  = std::shared_ptr<CreateRawSubClient>;
    using Spec = common::svc::relay::CreateRawSubSpec;

    using Success = RawSubscriber::Ptr;
    using Failure = Error;
    using Result  = cetl::variant<Success, Failure>;

    CETL_NODISCARD static Ptr make(cetl::pmr::memory_resource&           memory,
                                   const common::ipc::ClientRouter::Ptr& ipc_router,
                                   const Spec::Request&                  request);

    CreateRawSubClient(CreateRawSubClient&&)                 = delete;
    CreateRawSubClient(const CreateRawSubClient&)            = delete;
    CreateRawSubClient& operator=(CreateRawSubClient&&)      = delete;
    CreateRawSubClient& operator=(const CreateRawSubClient&) = delete;

    virtual ~CreateRawSubClient() = default;

    template <typename Receiver>
    void submit(Receiver&& receiver)
    {
        submitImpl([receive = std::forward<Receiver>(receiver)](Result&& result) mutable {
            //
            receive(std::move(result));
        });
    }

protected:
    CreateRawSubClient() = default;

    virtual void submitImpl(std::function<void(Result&&)>&& receiver) = 0;

};  // CreateRawSubClient

}  // namespace relay
}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_RELAY_CREATE_RAW_SUB_CLIENT_HPP_INCLUDED
