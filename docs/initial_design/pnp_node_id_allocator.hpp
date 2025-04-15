namespace ocvsmd
{

/// Implementation detail: internally, the PnP allocator uses the Monitor because the Monitor continuously
/// maintains the mapping between node-IDs and their unique-IDs. It needs to subscribe to notifications from the
/// monitor; this is not part of the API though. See pycyphal.application.plug_and_play.Allocator.
class PnPNodeIDAllocator
{
public:
    /// Maps unique-ID <=> node-ID.
    /// For some node-IDs there may be no unique-ID (at least temporarily until a GetInfo response is received).
    /// The table includes the daemon's node as well.
    using UID = std::array<std::uint8_t, 16>;
    using Entry = std::tuple<std::uint16_t, std::optional<UID>>;
    using Table = std::pmr::vector<Entry>;

    /// The method is infallible because the corresponding publishers/subscribers are always active;
    /// when enabled==false, the allocator simply refuses to send responses.
    virtual void set_enabled(const bool enabled) = 0;
    virtual bool is_enabled() const = 0;

    /// The allocation table may or may not be persistent (retained across daemon restarts).
    virtual Table get_table() const = 0;

    /// Forget all allocations; the table will be rebuilt from the Monitor state.
    virtual void drop_table() = 0;
};

}
