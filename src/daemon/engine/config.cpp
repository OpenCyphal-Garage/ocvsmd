//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "config.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <spdlog/spdlog.h>
#include <toml.hpp>

#include <chrono>
#include <exception>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace
{

class ConfigImpl final : public Config
{
public:
    using TomlConf  = toml::ordered_type_config;
    using TomlValue = toml::basic_value<TomlConf>;

    ConfigImpl(std::string file_path, TomlValue&& root)
        : file_path_{std::move(file_path)}
        , root_{std::move(root)}
        , is_dirty_{false}
    {
    }

    // Config

    void save() override
    {
        if (is_dirty_)
        {
            try
            {
                root_["__meta__"]["last_modified"] = std::chrono::system_clock::now();

                const auto    cfg_str = format(root_);
                std::ofstream file{file_path_, std::ios_base::out | std::ios_base::binary};
                file << cfg_str;

                is_dirty_ = false;

            } catch (const std::exception& ex)
            {
                spdlog::error("Failed to save config. Error: {}", file_path_, ex.what());
            }
        }
    }

    auto getCyphalAppNodeId() const -> cetl::optional<CyphalApp::NodeId> override
    {
        return findImpl<CyphalApp::NodeId>("cyphal", "application", "node_id");
    }

    auto getCyphalAppUniqueId() const -> cetl::optional<CyphalApp::UniqueId> override
    {
        return findImpl<CyphalApp::UniqueId>("cyphal", "application", "unique_id");
    }

    void setCyphalAppUniqueId(const CyphalApp::UniqueId& unique_id) override
    {
        auto& toml_unique_id = root_["cyphal"]["application"]["unique_id"];
        toml_unique_id       = unique_id;
        for (auto& item : toml_unique_id.as_array())
        {
            item.as_integer_fmt().fmt = toml::integer_format::hex;
        }
        toml_unique_id.as_array_fmt().fmt = toml::array_format::oneline;

        is_dirty_ = true;
    }

    auto getCyphalTransportInterfaces() const -> std::vector<std::string> override
    {
        return find_or(root_, "cyphal", "transport", "interfaces", std::vector<std::string>{});
    }

    auto getCyphalTransportMtu() const -> cetl::optional<std::size_t> override
    {
        return findImpl<std::size_t>("cyphal", "transport", "mtu");
    }

    auto getFileServerRoots() const -> std::vector<std::string> override
    {
        return find_or(root_, "file_server", "roots", std::vector<std::string>{});
    }

    void setFileServerRoots(const std::vector<std::string>& roots) override
    {
        auto& toml_fs_roots = root_["file_server"]["roots"];
        toml_fs_roots       = roots;
        is_dirty_           = true;
    }

    auto getIpcConnections() const -> std::vector<std::string> override
    {
        return find_or(root_, "ipc", "connections", std::vector<std::string>{});
    }

    auto getLoggingFile() const -> cetl::optional<std::string> override
    {
        return findImpl<std::string>("logging", "file");
    }

    auto getLoggingLevel() const -> cetl::optional<std::string> override
    {
        return findImpl<std::string>("logging", "level");
    }

    auto getLoggingFlushLevel() const -> cetl::optional<std::string> override
    {
        return findImpl<std::string>("logging", "flush_level");
    }

private:
    template <typename T, typename... Keys>
    cetl::optional<T> findImpl(Keys&&... keys) const
    {
        try
        {
            return cetl::make_optional(toml::find<T>(root_, std::forward<Keys>(keys)...));

        } catch (...)
        {
            return cetl::nullopt;
        }
    }

    std::string file_path_;
    TomlValue   root_;
    bool        is_dirty_;

};  // ConfigImpl

}  // namespace

Config::Ptr Config::make(std::string file_path)
{
    auto root = toml::parse<ConfigImpl::TomlConf>(file_path);
    return std::make_shared<ConfigImpl>(std::move(file_path), std::move(root));
}

}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd