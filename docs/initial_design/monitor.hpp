#include <uavcan/node/Heartbeat_1.hpp>
#include <uavcan/node/GetInfo_1.hpp>

namespace ocvsmd
{

/// The monitor continuously maintains a list of online nodes in the network.
class Monitor
{
public:
    using Heartbeat = uavcan::node::Heartbeat_1;
    using NodeInfo  = uavcan::node::GetInfo_1::Response;

    /// A shadow represents the latest known state of the remote node.
    /// The info struct is available only if the node responded to a uavcan.node.GetInfo request since last bootup.
    /// GetInfo requests are sent continuously until a response is received.
    /// If heartbeat publications cease, the corresponding node is marked as offline.
    struct Shadow final
    {
        std::uint16_t node_id;

        bool is_online;  ///< If not online, the other fields contain the latest known information.

        std::chrono::system_clock::time_point last_heartbeat_at;
        Heartbeat             last_heartbeat;

        /// The info is automatically reset when the remote node is detected to have restarted.
        /// It is automatically re-populated as soon as a GetInfo response is received.
        struct Info final
        {
            std::chrono::system_clock::time_point received_at;
            NodeInfo                              info;
        };
        std::optional<Info> info;

        /// The port list is automatically reset when the remote node is detected to have restarted.
        /// It is automatically re-populated as soon as an update is received.
        struct PortList final
        {
            std::chrono::system_clock::time_point received_at;
            std::bitset<65536> publishers;
            std::bitset<65536> subscribers;
            std::bitset<512>   clients;
            std::bitset<512>   servers;
        };
        std::optional<PortList> port_list;
    };

    struct Snapshot final
    {
        /// If a node appears online at least once, it will be given a slot in the table permanently.
        /// If it goes offline, it will be retained in the table but it's is_online field will be false.
        /// The table is ordered by node-ID. Use binary search for fast lookup.
        std::pmr::vector<Shadow> table;
        std::tuple<Heartbeat, NodeInfo> daemon;
        bool has_anonymous;   ///< If any anonymous nodes are online (e.g., someone is trying to get a PnP node-ID allocation)
    };

    /// Returns a snapshot of the current network state plus the daemon's own node state.
    virtual Snapshot snap() const = 0;

    // TODO: Eventually, we could equip the monitor with snooping support so that we could also obtain:
    //  - Actual traffic per port.
    //  - Update node info and local register cache without sending separate requests.
    // Yakut does that with the help of the snooping support in PyCyphal, but LibCyphal does not currently have that capability.
};

}
