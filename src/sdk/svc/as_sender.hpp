//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_SVC_AS_SENDER_HPP_INCLUDED
#define OCVSMD_SDK_SVC_AS_SENDER_HPP_INCLUDED

#include "logging.hpp"

#include <ocvsmd/sdk/execution.hpp>

namespace ocvsmd
{
namespace sdk
{
namespace svc
{

/// Adapter for an IPC service client to be used as a sender.
///
template <typename SvcClient, typename Result>
class AsSender final : public SenderOf<Result>
{
public:
    AsSender(cetl::string_view op_name, typename SvcClient::Ptr&& svc_client, common::LoggerPtr logger)
        : op_name_{op_name}
        , svc_client_{std::forward<typename SvcClient::Ptr>(svc_client)}
        , logger_{std::move(logger)}
    {
    }

    void submitImpl(std::function<void(Result&&)>&& receiver) override
    {
        logger_->trace("Submitting `{}` operation.", op_name_);

        svc_client_->submit([this, receiver = std::move(receiver)](typename SvcClient::Result&& result) mutable {
            //
            logger_->trace("Received result of `{}` operation.", op_name_);
            receiver(std::move(result));
        });
    }

private:
    cetl::string_view       op_name_;
    typename SvcClient::Ptr svc_client_;
    common::LoggerPtr       logger_;

};  // AsSender

}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_AS_SENDER_HPP_INCLUDED
