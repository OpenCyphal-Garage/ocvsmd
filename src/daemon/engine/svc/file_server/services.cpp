//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "services.hpp"

#include "cyphal/file_provider.hpp"
#include "list_roots_service.hpp"
#include "pop_root_service.hpp"
#include "push_root_service.hpp"
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

void registerAllServices(const ScvContext& context, cyphal::FileProvider& file_provider)
{
    ListRootsService::registerWithContext(context, file_provider);
    PopRootService::registerWithContext(context, file_provider);
    PushRootService::registerWithContext(context, file_provider);
}

}  // namespace file_server
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
