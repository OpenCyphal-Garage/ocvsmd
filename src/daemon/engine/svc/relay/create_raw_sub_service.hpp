//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_SVC_RELAY_CREATE_RAW_SUB_SERVICE_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_SVC_RELAY_CREATE_RAW_SUB_SERVICE_HPP_INCLUDED

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

/// Defines registration factory of the 'Relay: Create Raw Subscriber' service.
///
class CreateRawSubService
{
public:
    CreateRawSubService() = delete;
    static void registerWithContext(const ScvContext& context);

};  // CreateRawSubService

}  // namespace relay
}  // namespace svc
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_SVC_RELAY_CREATE_RAW_SUB_SERVICE_HPP_INCLUDED
