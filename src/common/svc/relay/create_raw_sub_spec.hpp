//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_SVC_RELAY_CREATE_RAW_SUB_SPEC_HPP_INCLUDED
#define OCVSMD_COMMON_SVC_RELAY_CREATE_RAW_SUB_SPEC_HPP_INCLUDED

#include "ocvsmd/common/svc/relay/CreateRawSub_0_1.hpp"

namespace ocvsmd
{
namespace common
{
namespace svc
{
namespace relay
{

/// Defines IPC internal housekeeping specification for the `CreateRawSub` service.
///
struct CreateRawSubSpec
{
    using Request  = CreateRawSub::Request_0_1;
    using Response = CreateRawSub::Response_0_1;

    constexpr auto static svc_full_name()
    {
        return "ocvsmd.svc.relay.create_raw_sub";
    }

    CreateRawSubSpec() = delete;
};

}  // namespace relay
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_SVC_RELAY_CREATE_RAW_SUB_SPEC_HPP_INCLUDED
