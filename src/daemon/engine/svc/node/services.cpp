//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "services.hpp"

#include "exec_cmd_service.hpp"
#include "svc/svc_helpers.hpp"

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace node
{

void registerAllServices(const ScvContext& context)
{
    ExecCmdService::registerWithContext(context);
}

}  // namespace node
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
