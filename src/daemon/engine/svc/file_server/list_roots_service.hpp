//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_SVC_FILE_SERVER_LIST_ROOTS_SERVICE_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_SVC_FILE_SERVER_LIST_ROOTS_SERVICE_HPP_INCLUDED

#include "cyphal/file_provider.hpp"
#include "svc/svc_helpers.hpp"

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace file_server
{

class ListRootService
{
public:
    ListRootService() = delete;
    static void registerWithContext(const ScvContext& context, cyphal::FileProvider& file_provider);

};  // ListRootService

}  // namespace file_server
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_SVC_FILE_SERVER_LIST_ROOTS_SERVICE_HPP_INCLUDED
