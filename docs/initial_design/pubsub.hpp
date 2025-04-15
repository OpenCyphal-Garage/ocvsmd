namespace ocvsmd
{
/// The daemon will implicitly instantiate a publisher on the specified port-ID the first time it is requested.
/// Once instantiated, the publisher may live on until the daemon process is terminated.
/// Internally, published messages may be transferred to the daemon via an IPC message queue.
class Publisher
{
public:
    /// True on success, false on timeout.
    virtual std::expected<bool, Error> publish(const std::span<const std::bytes> data,
                                               const std::chrono::microseconds timeout) = 0;
    std::expected<bool, Error> publish(const dsdl::Object& obj, const std::chrono::microseconds timeout)
    {
        // obj.serialize() and this->publish() ...
    }
};

/// The daemon will implicitly instantiate a subscriber on the specified port-ID the first time it is requested.
/// Once instantiated, the subscriber may live on until the daemon is terminated.
///
/// The daemon will associate an independent IPC queue with each client-side subscriber and push every received
/// message into the queues. Queues whose client has died are removed.
///
/// The client has to keep its Subscriber instance alive to avoid losing messages.
class Subscriber
{
public:
    /// Empty if no message received within the timeout.
    virtual std::expected<std::optional<Object>, Error> receive(const std::chrono::microseconds timeout) = 0;

    /// TODO: add methods for setting the transfer-ID timeout.
};

}
