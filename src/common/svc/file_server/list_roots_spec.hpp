//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_SVC_FILE_SERVER_LIST_ROOTS_SPEC_HPP_INCLUDED
#define OCVSMD_COMMON_SVC_FILE_SERVER_LIST_ROOTS_SPEC_HPP_INCLUDED

#include "ocvsmd/common/svc/file_server/ListRoots_0_1.hpp"

namespace ocvsmd
{
namespace common
{
namespace svc
{
namespace file_server
{

struct ListRootsSpec
{
    using Request  = ListRoots::Request_0_1;
    using Response = ListRoots::Response_0_1;

    constexpr auto static svc_full_name()
    {
        return "ocvsmd.svc.file_server.list_roots";
    }

    ListRootsSpec() = delete;
};

}  // namespace file_server
}  // namespace svc
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_SVC_FILE_SERVER_LIST_ROOTS_SPEC_HPP_INCLUDED
