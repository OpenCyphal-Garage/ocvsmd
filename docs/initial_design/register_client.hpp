#include <uavcan/_register/Value_1.hpp>
#include <uavcan/_register/Name_1.hpp>

namespace ocvsmd
{

/// A long operation may fail with partial results being available. The conventional approach to error handling
/// prescribes that the partial results are to be discarded and the error returned. However, said partial
/// results may occasionally be useful, if only to provide the additional context for the error itself.
///
/// This type allows one to model the normal results obtained from querying multiple remote nodes along with
/// non-exclusive error states both per-node and shared.
///
/// One alternative is to pass the output container or a callback as an out-parameter to the method,
/// so that the method does not return a new container but updates one in-place.
template <typename PerNodeResult, typename PerNodeError, typename SharedError>
struct MulticastResult
{
    struct PerNode final
    {
        PerNodeResult result{};
        PerNodeError error{};
    };
    std::pmr::unordered_map<std::uint16_t, PerNode> result;
    std::optional<Error> error;
};

/// A helper for manipulating registers on the specified remote nodes.
class RegisterClient
{
public:
    using Name  = uavcan::_register::Name_1;
    using Value = uavcan::_register::Value_1;

    /// This method may return partial results.
    virtual MulticastResult<std::pmr::vector<Name>, Error, Error>
        list(const std::span<const std::uint16_t> node_ids) = 0;

    /// Alternative definition of the list method using callbacks instead of partial results.
    /// The callbacks do not have to be invoked in real-time to simplify the IPC interface;
    /// instead, they can be postponed until the blocking IPC call is finished.
    using ListCallback = cetl::function<void(std::uint16_t, const std::expected<Name, Error>), ...>;
    virtual std::expected<void, Error> list(const std::span<const std::uint16_t> node_ids, const ListCallback& cb) = 0;

    /// This method may return partial results.
    virtual MulticastResult<std::pmr::unordered_map<Name, Value>, Error, Error>
        read(const std::span<const std::uint16_t> node_ids,
             const std::pmr::vector<Name>& names) = 0;

    /// This method may return partial results.
    virtual MulticastResult<std::pmr::unordered_map<Name, Value>, Error, Error>
        write(const std::span<const std::uint16_t> node_ids,
              const std::pmr::unordered_map<Name, Value>& values) = 0;

    /// Alternative definitions of the read/write methods using callbacks instead of partial results.
    /// The callbacks do not have to be invoked in real-time to simplify the IPC interface;
    /// instead, they can be postponed until the blocking IPC call is finished.
    using ValueCallback = cetl::function<void(std::uint16_t, const Name&, const std::expected<Value, Error>&), ...>;
    virtual std::expected<void, Error> read(const std::span<const std::uint16_t> node_ids,
                                            const std::pmr::vector<Name>& names,
                                            const ValueCallback& cb) = 0;
    virtual std::expected<void, Error> write(const std::span<const std::uint16_t> node_ids,
                                             const std::pmr::unordered_map<Name, Value>& values,
                                             const ValueCallback& cb) = 0;

    /// Helper wrappers for the above that operate on a single remote node only.
    std::tuple<std::pmr::vector<Name>, std::optional<Error>> list(const std::uint16_t node_id)
    {
        // non-virtual implementation
    }
    std::tuple<std::pmr::unordered_map<Name, Value>, Error>
        read(const std::uint16_t node_id, const std::pmr::vector<Name>& names)
    {
        // non-virtual implementation
    }
    std::tuple<std::pmr::unordered_map<Name, Value>, Error>
        write(const std::uint16_t node_id, const std::pmr::unordered_map<Name, Value>& values)
    {
        // non-virtual implementation
    }
};

}
