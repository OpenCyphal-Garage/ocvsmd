#include <uavcan/node/ExecuteCommand_1.hpp>

namespace ocvsmd
{

/// A helper for invoking the uavcan.node.ExecuteCommand service on the specified remote nodes.
/// The daemon always has a set of uavcan.node.ExecuteCommand clients ready.
class NodeCommandClient
{
public:
    using Request = uavcan::node::ExecuteCommand_1::Request;
    using Response = uavcan::node::ExecuteCommand_1::Response;

    /// Empty response indicates that the associated node did not respond in time.
    using Result = std::expected<std::pmr::unordered_map<std::uint16_t, std::optional<Response>>, Error>;

    /// Empty option indicates that the corresponding node did not return a response on time.
    /// All requests are sent concurrently and the call returns when the last response has arrived,
    /// or the timeout has expired.
    virtual Result send_custom_command(const std::span<const std::uint16_t> node_ids,
                                       const Request& request,
                                       const std::chrono::microseconds timeout = 1s) = 0;

    /// A convenience method for invoking send_custom_command() with COMMAND_RESTART.
    Result restart(const std::span<const std::uint16_t> node_ids, const std::chrono::microseconds timeout = 1s)
    {
        return send_custom_command(node_ids, {65535, ""}, timeout);
    }

    /// A convenience method for invoking send_custom_command() with COMMAND_BEGIN_SOFTWARE_UPDATE.
    /// The file_path is relative to one of the roots configured in the file server.
    Result begin_software_update(const std::span<const std::uint16_t> node_ids,
                                 const std::string_view file_path,
                                 const std::chrono::microseconds timeout = 1s)
    {
        return send_custom_command(node_ids, {65533, file_path}, timeout);
    }

    // TODO: add convenience methods for the other standard commands.
};

}
