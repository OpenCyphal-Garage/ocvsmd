//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_DAEMON_ENGINE_CYPHAL_TRANSPORT_HELPERS_HPP_INCLUDED
#define OCVSMD_DAEMON_ENGINE_CYPHAL_TRANSPORT_HELPERS_HPP_INCLUDED

#include "logging.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/errors.hpp>
#include <libcyphal/transport/can/can_transport.hpp>
#include <libcyphal/transport/errors.hpp>
#include <libcyphal/transport/udp/udp_transport.hpp>

#include <spdlog/fmt/fmt.h>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace cyphal
{

struct TransportHelpers
{
    struct Printers
    {
        static std::string describeError(const libcyphal::ArgumentError&)
        {
            return "ArgumentError";
        }
        static std::string describeError(const libcyphal::MemoryError&)
        {
            return "MemoryError";
        }
        static std::string describeError(const libcyphal::transport::AnonymousError&)
        {
            return "AnonymousError";
        }
        static std::string describeError(const libcyphal::transport::CapacityError&)
        {
            return "CapacityError";
        }
        static std::string describeError(const libcyphal::transport::AlreadyExistsError&)
        {
            return "AlreadyExistsError";
        }
        static std::string describeError(const libcyphal::transport::PlatformError& error)
        {
            return fmt::format("PlatformError(code={})", error->code());
        }

        static std::string describeAnyFailure(const libcyphal::transport::AnyFailure& failure)
        {
            return cetl::visit([](const auto& error) { return describeError(error); }, failure);
        }

    };  // Printers

#ifdef __linux__

    struct CanTransientErrorReporter
    {
        using Report = libcyphal::transport::can::ICanTransport::TransientErrorReport;

        cetl::optional<libcyphal::transport::AnyFailure> operator()(const Report::Variant& report_var) const
        {
            cetl::visit([this](const auto& report) { return log(report); }, report_var);
            return cetl::nullopt;
        }

    private:
        void log(const Report::CanardTxPush& report) const
        {
            logger_->error("Failed to push TX frame to canard (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::CanardRxAccept& report) const
        {
            logger_->error("Failed to accept RX frame at canard (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::MediaPop& report) const
        {
            logger_->error("Failed to pop frame from media (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::ConfigureMedia& report) const
        {
            logger_->error("Failed to configure CAN: {}.", Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::MediaConfig& report) const
        {
            logger_->error("Failed to configure media (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::MediaPush& report) const
        {
            logger_->error("Failed to push frame to media (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }

        common::LoggerPtr logger_{common::getLogger("io")};

    };  // CanTransientErrorReporter

#endif  // __linux__

    struct UdpTransientErrorReporter
    {
        using Report = libcyphal::transport::udp::IUdpTransport::TransientErrorReport;

        cetl::optional<libcyphal::transport::AnyFailure> operator()(const Report::Variant& report_var) const
        {
            cetl::visit([this](const auto& report) { return log(report); }, report_var);
            return cetl::nullopt;
        }

    private:
        void log(const Report::UdpardTxPublish& report) const
        {
            logger_->error("Failed to TX message frame to udpard (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::UdpardTxRequest& report) const
        {
            logger_->error("Failed to TX request frame to udpard (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::UdpardTxRespond& report) const
        {
            logger_->error("Failed to TX response frame to udpard (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::UdpardRxMsgReceive& report) const
        {
            logger_->error("Failed to accept RX message frame at udpard: {}.",
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::UdpardRxSvcReceive& report) const
        {
            logger_->error("Failed to accept RX service frame at udpard (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::MediaMakeRxSocket& report) const
        {
            logger_->error("Failed to make RX socket (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::MediaMakeTxSocket& report) const
        {
            logger_->error("Failed to make TX socket (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::MediaTxSocketSend& report) const
        {
            logger_->error("Failed to TX frame to socket (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }
        void log(const Report::MediaRxSocketReceive& report) const
        {
            logger_->error("Failed to RX frame from socket (mediaIdx={}): {}.",
                           report.media_index,
                           Printers::describeAnyFailure(report.failure));
        }

        common::LoggerPtr logger_{common::getLogger("io")};

    };  // UdpTransientErrorReporter

};  // TransportHelpers

}  // namespace cyphal
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd

#endif  // OCVSMD_DAEMON_ENGINE_CYPHAL_TRANSPORT_HELPERS_HPP_INCLUDED
