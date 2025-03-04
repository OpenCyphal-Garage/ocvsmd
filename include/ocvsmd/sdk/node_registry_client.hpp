//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_REGISTRY_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_NODE_REGISTRY_CLIENT_HPP_INCLUDED

#include <memory>

namespace ocvsmd
{
namespace sdk
{

/// Defines client side interface of the OCVSMD Node Registry component.
///
class NodeRegistryClient
{
public:
    /// Defines the shared pointer type for the interface.
    ///
    using Ptr = std::shared_ptr<NodeRegistryClient>;

    NodeRegistryClient(NodeRegistryClient&&)                 = delete;
    NodeRegistryClient(const NodeRegistryClient&)            = delete;
    NodeRegistryClient& operator=(NodeRegistryClient&&)      = delete;
    NodeRegistryClient& operator=(const NodeRegistryClient&) = delete;

    virtual ~NodeRegistryClient() = default;

protected:
    NodeRegistryClient() = default;

};  // NodeRegistryClient

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_REGISTRY_CLIENT_HPP_INCLUDED
