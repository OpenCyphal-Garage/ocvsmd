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
template <typename Result, typename SvcClientPtr>
class AsSender final : public SenderOf<Result>
{
public:
    AsSender(cetl::string_view op_name, SvcClientPtr&& svc_client, common::LoggerPtr logger)
        : op_name_{op_name}
        , svc_client_{std::forward<SvcClientPtr>(svc_client)}
        , logger_{std::move(logger)}
    {
    }

    void submitImpl(std::function<void(Result&&)>&& receiver) override
    {
        logger_->trace("Submitting `{}` operation.", op_name_);

        svc_client_->submit([this, receiver = std::move(receiver)](Result&& result) mutable {
            //
            logger_->trace("Received result of `{}` operation.", op_name_);
            receiver(std::move(result));
        });
    }

private:
    cetl::string_view op_name_;
    SvcClientPtr      svc_client_;
    common::LoggerPtr logger_;

};  // AsSender

}  // namespace svc
}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_SVC_AS_SENDER_HPP_INCLUDED
