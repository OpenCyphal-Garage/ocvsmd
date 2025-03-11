//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "fmt_helpers.hpp"
#include "setup_logging.hpp"

#include <ocvsmd/platform/defines.hpp>
#include <ocvsmd/sdk/daemon.hpp>
#include <ocvsmd/sdk/defines.hpp>
#include <ocvsmd/sdk/execution.hpp>
#include <ocvsmd/sdk/node_command_client.hpp>

#include <cetl/pf17/cetlpf.hpp>
#include <cetl/pf20/cetlpf.hpp>

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <signal.h>  // NOLINT
#include <set>
#include <utility>
#include <vector>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

namespace
{

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile sig_atomic_t g_running = 1;

void signalHandler(const int sig)
{
    switch (sig)
    {
    case SIGINT:
    case SIGTERM:
        g_running = 0;
        break;
    default:
        break;
    }
}

void setupSignalHandlers()
{
    struct sigaction sigbreak
    {};
    sigbreak.sa_handler = &signalHandler;
    ::sigaction(SIGINT, &sigbreak, nullptr);
    ::sigaction(SIGTERM, &sigbreak, nullptr);
}

void logCommandResult(const ocvsmd::sdk::NodeCommandClient::Command::Result& cmd_result)
{
    using Command = ocvsmd::sdk::NodeCommandClient::Command;

    if (const auto* const err = cetl::get_if<Command::Failure>(&cmd_result))
    {
        spdlog::error("Failed to send command: {}", std::strerror(*err));
        return;
    }

    const auto& responds = cetl::get<Command::Success>(cmd_result);
    for (const auto& node_and_respond : responds)
    {
        if (const auto* const err = cetl::get_if<1>(&node_and_respond.second))
        {
            spdlog::warn("{:4} → err={}", node_and_respond.first, *err);
            continue;
        }
        const auto& response = cetl::get<0>(node_and_respond.second);
        spdlog::info("{:4} → status={}.", node_and_respond.first, response.status);
    }
}

void setRegisterValue(ocvsmd::sdk::NodeRegistryClient::Access::RegValue& reg_val, const std::string& text)
{
    auto& reg_str = reg_val.set_string();
    std::copy(text.begin(), text.end(), std::back_inserter(reg_str.value));
}

void logRegistryListNodeResult(  //
    std::set<std::string>&                                              reg_names_set,
    const ocvsmd::sdk::CyphalNodeId                                     node_id,
    const ocvsmd::sdk::NodeRegistryClient::List::NodeRegisters::Result& result)
{
    using NodeRegisters = ocvsmd::sdk::NodeRegistryClient::List::NodeRegisters;

    if (const auto* const node_err = cetl::get_if<NodeRegisters::Failure>(&result))
    {
        spdlog::warn("{:4} → err={}", node_id, *node_err);
        return;
    }

    const auto& node_regs = cetl::get<NodeRegisters::Success>(result);
    for (const auto& reg_name : node_regs)
    {
        reg_names_set.insert(reg_name);
        spdlog::info("{:4} → '{}'", node_id, reg_name);
    }
}

void logRegistryAccessNodeResult(const ocvsmd::sdk::CyphalNodeId                                       node_id,
                                 const ocvsmd::sdk::NodeRegistryClient::Access::NodeRegisters::Result& result)
{
    using NodeRegs = ocvsmd::sdk::NodeRegistryClient::Access::NodeRegisters;

    if (const auto* const node_err = cetl::get_if<NodeRegs::Failure>(&result))
    {
        spdlog::warn("{:4} → err={}", node_id, *node_err);
        return;
    }
    const auto& node_reg_vals = cetl::get<NodeRegs::Success>(result);

    for (const auto& reg_key_val : node_reg_vals)
    {
        if (const auto* const reg_err = cetl::get_if<1>(&reg_key_val.value_or_err))
        {
            spdlog::warn("{:4} → '{}' err={}", node_id, reg_key_val.key, *reg_err);
            continue;
        }
        const auto& reg_val = cetl::get<0>(reg_key_val.value_or_err);

        spdlog::info("{:4} → '{}'={}", node_id, reg_key_val.key, reg_val);
    }
}

void logRegistryAccessResult(ocvsmd::sdk::NodeRegistryClient::Access::Result&& result)
{
    using Access = ocvsmd::sdk::NodeRegistryClient::Access;

    const auto node_id_to_reg_vals = cetl::get<Access::Success>(std::move(result));
    spdlog::info("Engine responded with list of nodes (cnt={}):", node_id_to_reg_vals.size());
    for (const auto& id_and_reg_vals : node_id_to_reg_vals)
    {
        logRegistryAccessNodeResult(id_and_reg_vals.first, id_and_reg_vals.second);
    }
}

}  // namespace

int main(const int argc, const char** const argv)
{
    using std::chrono_literals::operator""s;
    using Executor = ocvsmd::platform::SingleThreadedExecutor;

    setupSignalHandlers();
    setupLogging(argc, argv);

    spdlog::info("OCVSMD client started (ver='{}.{}').", VERSION_MAJOR, VERSION_MINOR);
    int result = EXIT_SUCCESS;
    try
    {
        auto&    memory = *cetl::pmr::new_delete_resource();
        Executor executor;

        std::string ipc_connection = "unix-abstract:org.opencyphal.ocvsmd.ipc";
        if (const auto* const env_connection_str = std::getenv("OCVSMD_CONNECTION"))
        {
            ipc_connection = env_connection_str;
        }

        const auto daemon = ocvsmd::sdk::Daemon::make(memory, executor, ipc_connection);
        if (!daemon)
        {
            spdlog::critical("Failed to create daemon.");
            std::cerr << "Failed to create daemon.";
            return EXIT_FAILURE;
        }

#if 1  // NOLINT

        // Demo of daemon's node command client - sending a command to node 42, 43 & 44.
        {
            using Command = ocvsmd::sdk::NodeCommandClient::Command;

            auto node_cmd_client = daemon->getNodeCommandClient();

            std::array<ocvsmd::sdk::CyphalNodeId, 3> node_ids{42, 43, 44};
            // auto sender = node_cmd_client->restart(node_ids);
            auto sender     = node_cmd_client->beginSoftwareUpdate(node_ids, "firmware.bin");
            auto cmd_result = ocvsmd::sdk::sync_wait<Command::Result>(executor, std::move(sender));
            logCommandResult(cmd_result);
        }
#endif
#if 0  // NOLINT

        // Demo of daemon's file server - push root.
        {
            using PushRoot = ocvsmd::sdk::FileServer::PushRoot;

            auto file_server = daemon->getFileServer();

            const std::string path{"key"};
            auto              sender     = file_server->pushRoot(path, true);
            auto              cmd_result = ocvsmd::sdk::sync_wait<PushRoot::Result>(executor, std::move(sender));
            if (const auto* const err = cetl::get_if<PushRoot::Failure>(&cmd_result))
            {
                spdlog::error("Failed to push FS root (path='{}'): {}.", path, std::strerror(*err));
            }
            else
            {
                spdlog::info("File Server responded ok on 'PushRoot'.");
            }
        }
#endif
#if 0  // NOLINT

        // Demo of daemon's file server - pop root.
        {
            using PopRoot = ocvsmd::sdk::FileServer::PopRoot;

            auto file_server = daemon->getFileServer();

            const std::string path{"key"};
            auto              sender     = file_server->popRoot(path, true);
            auto              cmd_result = ocvsmd::sdk::sync_wait<PopRoot::Result>(executor, std::move(sender));
            if (const auto* const err = cetl::get_if<PopRoot::Failure>(&cmd_result))
            {
                spdlog::error("Failed to pop FS root (path='{}'): {}.", path, std::strerror(*err));
            }
            else
            {
                spdlog::info("File Server responded ok on 'PopRoot'.");
            }
        }
#endif
#if 0  // NOLINT

        // Demo of daemon's file server - getting the list of roots.
        {
            using ListRoots = ocvsmd::sdk::FileServer::ListRoots;

            auto file_server = daemon->getFileServer();

            auto sender     = file_server->listRoots();
            auto cmd_result = ocvsmd::sdk::sync_wait<ListRoots::Result>(executor, std::move(sender));
            if (const auto* const err = cetl::get_if<ListRoots::Failure>(&cmd_result))
            {
                spdlog::error("Failed to list FS roots: {}", std::strerror(*err));
            }
            else
            {
                const auto roots = cetl::get<ListRoots::Success>(std::move(cmd_result));
                spdlog::info("File Server responded with list of roots (cnt={}):", roots.size());
                for (std::size_t i = 0; i < roots.size(); ++i)
                {
                    spdlog::info("{:4} → '{}'", i, roots[i]);
                }
            }
        }
#endif
#if 0  // NOLINT

        // Demo of daemon's registry client - getting the list of registers from nodes.
        {
            using List   = ocvsmd::sdk::NodeRegistryClient::List;
            using Access = ocvsmd::sdk::NodeRegistryClient::Access;

            auto registry = daemon->getNodeRegistryClient();

            // List ALL registers.
            //
            std::array<ocvsmd::sdk::CyphalNodeId, 3> node_ids{42, 43, 44};
            //
            auto sender      = registry->list(node_ids, std::chrono::seconds{1});
            auto list_result = ocvsmd::sdk::sync_wait<List::Result>(executor, std::move(sender));
            if (const auto* const list_err = cetl::get_if<List::Failure>(&list_result))
            {
                spdlog::error("Failed to list registers: {}", std::strerror(*list_err));
            }
            else
            {
                std::set<std::string> reg_names_set;
                const auto            node_id_to_regs = cetl::get<List::Success>(std::move(list_result));
                spdlog::info("Engine responded with list of nodes (cnt={}):", node_id_to_regs.size());
                for (const auto& id_and_regs : node_id_to_regs)
                {
                    logRegistryListNodeResult(reg_names_set, id_and_regs.first, id_and_regs.second);
                }
                const std::vector<cetl::string_view>      reg_names_vec{reg_names_set.begin(), reg_names_set.end()};
                const cetl::span<const cetl::string_view> reg_names{reg_names_vec.data(), reg_names_vec.size()};

                // Read ALL registers.
                //
                auto read_sender = registry->read(node_ids, reg_names, std::chrono::seconds{1});
                auto read_result = ocvsmd::sdk::sync_wait<Access::Result>(executor, std::move(read_sender));
                if (const auto* const read_err = cetl::get_if<Access::Failure>(&read_result))
                {
                    spdlog::error("Failed to read registers: {}", std::strerror(*read_err));
                }
                else
                {
                    logRegistryAccessResult(std::move(read_result));
                }

                // Write 'uavcan.node.description' registers.
                //
                const cetl::string_view            reg_key_desc{"uavcan.node.description"};
                std::array<Access::RegKeyValue, 1> reg_keys_and_values{
                    Access::RegKeyValue{reg_key_desc, Access::RegValue{&memory}}};
                setRegisterValue(reg_keys_and_values[0].value, "libcyphal demo node3");
                //
                auto write_sender = registry->write(node_ids, reg_keys_and_values, std::chrono::seconds{1});
                auto write_result = ocvsmd::sdk::sync_wait<Access::Result>(executor, std::move(write_sender));
                if (const auto* const write_err = cetl::get_if<Access::Failure>(&write_result))
                {
                    spdlog::error("Failed to write registers: {}", std::strerror(*write_err));
                }
                else
                {
                    logRegistryAccessResult(std::move(write_result));
                }
            }

            // Try single node id api.
            {
                constexpr ocvsmd::sdk::CyphalNodeId node_id{42};
                std::array<cetl::string_view, 1>    reg_keys{"uavcan.node.description"};

                // List
                {
                    auto list_node_sender = registry->list(node_id, std::chrono::seconds{1});
                    auto list_node_result = ocvsmd::sdk::sync_wait<List::NodeRegisters::Result>(  //
                        executor,
                        std::move(list_node_sender));

                    std::set<std::string> reg_names_set;
                    logRegistryListNodeResult(reg_names_set, node_id, list_node_result);
                }

                // Write
                {
                    const cetl::string_view            reg_key_desc{"uavcan.node.description"};
                    std::array<Access::RegKeyValue, 1> reg_keys_and_values{
                        Access::RegKeyValue{reg_key_desc, Access::RegValue{&memory}}};
                    setRegisterValue(reg_keys_and_values[0].value, "libcyphal demo node42");

                    auto write_node_sender = registry->write(node_id, reg_keys_and_values, std::chrono::seconds{1});
                    auto write_node_result = ocvsmd::sdk::sync_wait<Access::NodeRegisters::Result>(  //
                        executor,
                        std::move(write_node_sender));

                    logRegistryAccessNodeResult(node_id, write_node_result);
                }

                // Read
                {
                    auto read_node_sender = registry->read(node_id, reg_keys, std::chrono::seconds{1});
                    auto read_node_result = ocvsmd::sdk::sync_wait<Access::NodeRegisters::Result>(  //
                        executor,
                        std::move(read_node_sender));
                    logRegistryAccessNodeResult(node_id, read_node_result);
                }
            }
        }
#endif

        if (g_running == 0)
        {
            spdlog::debug("Received termination signal.");
        }

    } catch (const std::exception& ex)
    {
        spdlog::critical("Unhandled exception: {}", ex.what());
        result = EXIT_FAILURE;
    }
    spdlog::info("OCVSMD client terminated.");

    return result;
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
