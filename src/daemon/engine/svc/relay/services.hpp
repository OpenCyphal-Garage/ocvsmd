//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_SVC_RELAY_SERVICES_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_SVC_RELAY_SERVICES_HPP_INCLUDED

#include "svc/svc_helpers.hpp"

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace svc
{
namespace relay
{

/// Registers all "relay"-related services.
///
void registerAllServices(const ScvContext& context);

}  // namespace relay
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_SVC_RELAY_SERVICES_HPP_INCLUDED
