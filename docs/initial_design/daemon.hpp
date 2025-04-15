namespace ocvsmd
{

/// An abstract factory for the specialized interfaces.
class Daemon
{
public:
    virtual std::expected<std::unique_ptr<Publisher>, Error> make_publisher(const dsdl::Type& type,
                                                                            const std::uint16_t subject_id) = 0;

    virtual std::expected<std::unique_ptr<Subscriber>, Error> make_subscriber(const dsdl::Type& type,
                                                                              const std::uint16_t subject_id) = 0;

    virtual std::expected<std::unique_ptr<RPCClient>, Error> make_client(const dsdl::Type& type,
                                                                         const std::uint16_t server_node_id,
                                                                         const std::uint16_t service_id) = 0;

    virtual       FileServer& get_file_server() = 0;
    virtual const FileServer& get_file_server() const = 0;

    virtual NodeCommandClient& get_node_command_client() = 0;

    virtual RegisterClient& get_register_client() = 0;

    virtual       Monitor& get_monitor() = 0;
    virtual const Monitor& get_monitor() const = 0;

    virtual       PnPNodeIDAllocator& get_pnp_node_id_allocator() = 0;
    virtual const PnPNodeIDAllocator& get_pnp_node_id_allocator() const = 0;
};

/// A factory for the abstract factory that connects to the daemon.
/// The pointer is never null on success.
std::expected<std::unique_ptr<Daemon>, Error> connect();
}
