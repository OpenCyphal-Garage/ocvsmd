//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_HPP_INCLUDED

#include "config.hpp"
#include "cyphal/any_transport_bag.hpp"
#include "cyphal/file_provider.hpp"
#include "logging.hpp"
#include "ocvsmd/platform/defines.hpp"

#include <ipc/server_router.hpp>

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/application/node.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/transport/transfer_id_map.hpp>
#include <libcyphal/transport/types.hpp>

#include <functional>
#include <string>
#include <unordered_map>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{

class Engine
{
public:
    explicit Engine(Config::Ptr config);

    CETL_NODISCARD cetl::optional<std::string> init();
    void                                       runWhile(const std::function<bool()>& loop_predicate);

private:
    using UniqueId = Config::CyphalApp::UniqueId;

    class TransferIdMap final : public libcyphal::transport::ITransferIdMap
    {
        using TransferId = libcyphal::transport::TransferId;
        using Map        = std::unordered_map<SessionSpec, TransferId>;

        // ITransferIdMap

        TransferId getIdFor(const SessionSpec& session_spec) const noexcept override
        {
            const auto it = session_spec_to_transfer_id_.find(session_spec);
            return it != session_spec_to_transfer_id_.end() ? it->second : 0;
        }

        void setIdFor(const SessionSpec& session_spec, const TransferId transfer_id) noexcept override
        {
            session_spec_to_transfer_id_[session_spec] = transfer_id;
        }

        Map session_spec_to_transfer_id_;

    };  // TransferIdMap

    UniqueId getUniqueId() const;

    Config::Ptr                                           config_;
    common::LoggerPtr                                     logger_{common::getLogger("engine")};
    platform::SingleThreadedExecutor                      executor_;
    cetl::pmr::memory_resource&                           memory_{*cetl::pmr::get_default_resource()};
    cyphal::AnyTransportBag::Ptr                          any_transport_bag_;
    TransferIdMap                                         transfer_id_map_;
    cetl::optional<libcyphal::presentation::Presentation> presentation_;
    cetl::optional<libcyphal::application::Node>          node_;
    cyphal::FileProvider::Ptr                             file_provider_;
    common::ipc::ServerRouter::Ptr                        ipc_router_;

};  // Engine

}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_HPP_INCLUDED
