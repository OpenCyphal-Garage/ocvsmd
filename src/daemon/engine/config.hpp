//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_CONFIG_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_CONFIG_HPP_INCLUDED

#include <cetl/cetl.hpp>
#include <cetl/pf17/cetlpf.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{

class Config
{
public:
    using Ptr = std::shared_ptr<Config>;

    struct CyphalApp
    {
        using NodeId   = std::uint16_t;
        using UniqueId = std::array<std::uint8_t, 16>;  // NOLINT(*-magic-numbers)
    };

    CETL_NODISCARD static Ptr make(std::string file_path);

    Config(const Config&)                = delete;
    Config(Config&&) noexcept            = delete;
    Config& operator=(const Config&)     = delete;
    Config& operator=(Config&&) noexcept = delete;

    virtual ~Config() = default;

    virtual void save() = 0;

    CETL_NODISCARD virtual auto getCyphalAppNodeId() const -> cetl::optional<CyphalApp::NodeId>     = 0;
    CETL_NODISCARD virtual auto getCyphalAppUniqueId() const -> cetl::optional<CyphalApp::UniqueId> = 0;
    virtual void                setCyphalAppUniqueId(const CyphalApp::UniqueId& unique_id)          = 0;

    CETL_NODISCARD virtual auto getCyphalTransportInterfaces() const -> std::vector<std::string> = 0;
    CETL_NODISCARD virtual auto getCyphalTransportMtu() const -> cetl::optional<std::size_t>     = 0;

    CETL_NODISCARD virtual auto getFileServerRoots() const -> std::vector<std::string>    = 0;
    virtual void                setFileServerRoots(const std::vector<std::string>& roots) = 0;

    CETL_NODISCARD virtual auto getIpcConnections() const -> std::vector<std::string> = 0;

    CETL_NODISCARD virtual auto getLoggingFile() const -> cetl::optional<std::string>       = 0;
    CETL_NODISCARD virtual auto getLoggingLevel() const -> cetl::optional<std::string>      = 0;
    CETL_NODISCARD virtual auto getLoggingFlushLevel() const -> cetl::optional<std::string> = 0;

protected:
    Config() = default;

};  // Config

}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_CONFIG_HPP_INCLUDED
