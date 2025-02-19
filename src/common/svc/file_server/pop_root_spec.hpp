//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_SVC_FILE_SERVER_POP_ROOT_SPEC_HPP_INCLUDED
#define OCVSMD_COMMON_SVC_FILE_SERVER_POP_ROOT_SPEC_HPP_INCLUDED

#include "ocvsmd/common/svc/file_server/PopRoot_0_1.hpp"

namespace ocvsmd
{
namespace common
{
namespace svc
{
namespace file_server
{

struct PopRootSpec
{
    using Request  = PopRoot::Request_0_1;
    using Response = PopRoot::Response_0_1;

    constexpr auto static svc_full_name()
    {
        return "ocvsmd.svc.file_server.pop_root";
    }

    PopRootSpec() = delete;
};

}  // namespace file_server
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_SVC_FILE_SERVER_POP_ROOT_SPEC_HPP_INCLUDED
