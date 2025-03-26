//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED
#define OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED

#include "execution.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <memory>

namespace ocvsmd
{
namespace sdk
{

class RawSubscriber
{
public:
    using Ptr = std::shared_ptr<RawSubscriber>;

    virtual ~RawSubscriber() = default;

    // No copy/move semantics.
    RawSubscriber(RawSubscriber&&)                 = delete;
    RawSubscriber(const RawSubscriber&)            = delete;
    RawSubscriber& operator=(RawSubscriber&&)      = delete;
    RawSubscriber& operator=(const RawSubscriber&) = delete;

    struct Receive final
    {
        struct Success
        {
            std::size_t                   size;
            std::unique_ptr<cetl::byte[]> data;  // NOLINT(*-avoid-c-arrays)
            CyphalPriority                priority;
            cetl::optional<CyphalNodeId>  publisher_node_id;
        };
        using Failure = Error;
        using Result  = cetl::variant<Success, Failure>;
    };
    virtual SenderOf<Receive::Result>::Ptr receive() = 0;

protected:
    RawSubscriber() = default;

};  // RawSubscriber

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_NODE_PUB_SUB_HPP_INCLUDED
