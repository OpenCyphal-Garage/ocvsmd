//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_REGISTRY_CLIENT_HPP_INCLUDED
#define OCVSMD_SDK_NODE_REGISTRY_CLIENT_HPP_INCLUDED

#include "defines.hpp"
#include "execution.hpp"

#include <uavcan/_register/Value_1_0.hpp>

#include <cetl/pf20/cetlpf.hpp>

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

    /// Defines the result type of the list command execution.
    ///
    /// On success, the result is a map of node ID to its register names (or SDK error for the node).
    /// Missing Cyphal nodes (or failed to respond in a given timeout) are included in the map with ETIMEDOUT code.
    /// On failure, the result is a SDK error of some failure to communicate with the OCVSMD engine.
    ///
    struct List final
    {
        /// Defines the result type of the list of a node registers.
        ///
        /// On success, the result is a vector of register names.
        /// On failure, the result is a SDK error.
        ///
        struct NodeRegisters final
        {
            using Success = std::vector<std::string>;
            using Failure = Error;
            using Result  = cetl::variant<Success, Failure>;

            NodeRegisters() = delete;
        };

        using Success = std::unordered_map<CyphalNodeId, NodeRegisters::Result>;
        using Failure = Error;
        using Result  = cetl::variant<Success, Failure>;

        List() = delete;
    };
    /// Collects list of register names from the specified Cyphal network nodes.
    ///
    /// On the OCVSMD engine side, Cyphal `385.List.1.0` requests are sent concurrently to all specified Cyphal nodes.
    /// Responses are sent back to the client side as they arrive, and collected in the result map.
    /// The overall result will be available when the last response has arrived, or the timeout has expired.
    ///
    /// @param node_ids The set of Cyphal node IDs to be `list`-ed. Duplicates are ignored.
    /// @param timeout The maximum time to wait for all Cyphal node responses to arrive.
    /// @return An execution sender which emits the async overall result of the operation.
    ///
    virtual SenderOf<List::Result>::Ptr list(const CyphalNodeIds node_ids, const std::chrono::microseconds timeout) = 0;

    /// Collects list of register names from the specified Cyphal network node.
    ///
    /// Internally it calls the above `list` method - just provides more simple API:
    /// - input is just one node ID (instead of a span of ids);
    /// - output is just a vector of register names (or SDK error).
    ///
    SenderOf<List::NodeRegisters::Result>::Ptr list(const CyphalNodeId node_id, const std::chrono::microseconds timeout)
    {
        std::array<CyphalNodeId, 1> node_ids{node_id};
        SenderOf<List::Result>::Ptr sender = list(node_ids, timeout);
        return then<List::NodeRegisters::Result, List::Result>(std::move(sender), [node_id](List::Result&& result) {
            //
            if (auto* const failure = cetl::get_if<List::Failure>(&result))
            {
                return List::NodeRegisters::Result{*failure};
            }
            auto list = cetl::get<List::Success>(std::move(result));

            const auto node_it = list.find(node_id);
            return (node_it != list.end()) ? std::move(node_it->second)
                                           : List::NodeRegisters::Failure{Error::Code::NoEntry};
        });
    }

    /// Defines the result type of the access (read/write) commands execution.
    ///
    /// On success, the result is a map of node ID to its register names and values (or SDK error for the node).
    /// Missing Cyphal nodes (or failed to respond in a given timeout) are included in the map with ETIMEDOUT code.
    /// On failure, the result is a SDK error of some failure to communicate with the OCVSMD engine.
    ///
    struct Access final
    {
        using RegValue = uavcan::_register::Value_1_0;

        /// Defines an input pair of a register name and (optionally) its value.
        ///
        struct RegKeyValue final
        {
            cetl::string_view key;
            RegValue          value;
        };

        /// Defines an output pair of a register name and its value (or SDK error).
        ///
        struct RegKeyValueOrErr final
        {
            std::string                    key;
            cetl::variant<RegValue, Error> value_or_err;
        };

        /// Defines the result type of the list of a node registers.
        ///
        /// On success, the result is a vector of register names and values (or errors).
        /// On failure, the result is a SDK error.
        ///
        struct NodeRegisters final
        {
            using Success = std::vector<RegKeyValueOrErr>;
            using Failure = Error;
            using Result  = cetl::variant<Success, Failure>;

            NodeRegisters() = delete;
        };

        using Success = std::unordered_map<CyphalNodeId, NodeRegisters::Result>;
        using Failure = Error;
        using Result  = cetl::variant<Success, Failure>;

        Access() = delete;
    };

    /// Reads a list of register values from the specified Cyphal network nodes.
    ///
    /// On the OCVSMD engine side, Cyphal `384.Access.1.0` requests are sent concurrently to all specified Cyphal nodes.
    /// Responses are sent back to the client side as they arrive, and collected in the result map.
    /// The overall result will be available when the last response has arrived, or the timeout has expired.
    ///
    /// @param node_ids The set of Cyphal node IDs to be `read`. Duplicates are ignored.
    /// @param registers The list of register names to be `read`.
    /// @param timeout The maximum time to wait for all Cyphal node responses to arrive.
    /// @return An execution sender which emits the async overall result of the operation.
    ///
    virtual SenderOf<Access::Result>::Ptr read(const CyphalNodeIds                       node_ids,
                                               const cetl::span<const cetl::string_view> registers,
                                               const std::chrono::microseconds           timeout) = 0;

    /// Reads a list of register names from the specified Cyphal network node.
    ///
    /// Internally it calls the above `read` method - just provides more simple API:
    /// - input is just one node ID (instead of a span of ids);
    /// - output is just a vector of the node register names and their values (or SDK error).
    ///
    SenderOf<Access::NodeRegisters::Result>::Ptr read(const CyphalNodeId                        node_id,
                                                      const cetl::span<const cetl::string_view> registers,
                                                      const std::chrono::microseconds           timeout)
    {
        std::array<CyphalNodeId, 1>   node_ids{node_id};
        SenderOf<Access::Result>::Ptr sender = read(node_ids, registers, timeout);
        return then<Access::NodeRegisters::Result, Access::Result>(  //
            std::move(sender),
            [node_id](Access::Result&& access_result) {
                //
                if (auto* const failure = cetl::get_if<Access::Failure>(&access_result))
                {
                    return Access::NodeRegisters::Result{*failure};
                }
                auto list = cetl::get<Access::Success>(std::move(access_result));

                const auto node_it = list.find(node_id);
                return (node_it != list.end()) ? std::move(node_it->second)
                                               : Access::NodeRegisters::Failure{Error::Code::NoEntry};
            });
    }

    /// Writes a list of register values to the specified Cyphal network nodes.
    ///
    /// On the OCVSMD engine side, Cyphal `384.Access.1.0` requests are sent concurrently to all specified Cyphal nodes.
    /// Responses are sent back to the client side as they arrive, and collected in the result map.
    /// The overall result will be available when the last response has arrived, or the timeout has expired.
    ///
    /// @param node_ids The set of Cyphal node IDs to be `read`. Duplicates are ignored.
    /// @param registers The list of register names and their values to be `written`.
    /// @param timeout The maximum time to wait for all Cyphal node responses to arrive.
    /// @return An execution sender which emits the async overall result of the operation.
    ///
    virtual SenderOf<Access::Result>::Ptr write(const CyphalNodeIds                         node_ids,
                                                const cetl::span<const Access::RegKeyValue> registers,
                                                const std::chrono::microseconds             timeout) = 0;

    /// Writes a list of register names to the specified Cyphal network node.
    ///
    /// Internally it calls the above `write` method - just provides more simple API:
    /// - input is just one node ID (instead of a span of ids);
    /// - output is just a vector of the node register names and their values (or SDK error).
    ///
    SenderOf<Access::NodeRegisters::Result>::Ptr write(const CyphalNodeId                          node_id,
                                                       const cetl::span<const Access::RegKeyValue> registers,
                                                       const std::chrono::microseconds             timeout)
    {
        std::array<CyphalNodeId, 1>   node_ids{node_id};
        SenderOf<Access::Result>::Ptr sender = write(node_ids, registers, timeout);
        return then<Access::NodeRegisters::Result, Access::Result>(  //
            std::move(sender),
            [node_id](Access::Result&& access_result) {
                //
                if (auto* const failure = cetl::get_if<Access::Failure>(&access_result))
                {
                    return Access::NodeRegisters::Result{*failure};
                }
                auto list = cetl::get<Access::Success>(std::move(access_result));

                const auto node_it = list.find(node_id);
                return (node_it != list.end()) ? std::move(node_it->second)
                                               : Access::NodeRegisters::Failure{Error::Code::NoEntry};
            });
    }

protected:
    NodeRegistryClient() = default;

};  // NodeRegistryClient

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_REGISTRY_CLIENT_HPP_INCLUDED
