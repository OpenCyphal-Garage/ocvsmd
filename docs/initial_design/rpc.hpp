
namespace ocvsmd
{

/// The daemon will implicitly instantiate a client on the specified port-ID and server node when it is requested.
/// Once instantiated, the client may live on until the daemon is terminated.
class RPCClient
{
public:
    /// Returns the response object on success, empty on timeout.
    virtual std::expected<std::optional<Object>, Error> call(const dsdl::Object& obj,
                                                             const std::chrono::microseconds timeout) = 0;
};

}
