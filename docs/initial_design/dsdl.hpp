namespace ocvsmd::dsdl
{
/// Represents a DSDL object of any type.
class Object
{
    friend class Type;
public:
    /// Field accessor by name. Empty if no such field.
    std::optional<Object> operator[](const std::string_view field_name) const;

    /// Array element accessor by index. Empty if out of range.
    std::optional<std::span<Object>> operator[](const std::size_t array_index);
    std::optional<std::span<const Object>> operator[](const std::size_t array_index) const;

    /// Coercion to primitives (implicit truncation or the loss of precision are possible).
    operator std::optional<std::int64_t>() const;
    operator std::optional<std::uint64_t>() const;
    operator std::optional<double>() const;

    /// Coercion from primitives (implicit truncation or the loss of precision are possible).
    Object& operator=(const std::int64_t value);
    Object& operator=(const std::uint64_t value);
    Object& operator=(const double value);

    const class Type& get_type() const noexcept;

    std::expected<void, Error> serialize(const std::span<std::byte> output) const;
    std::expected<void, Error> deserialize(const std::span<const std::byte> input);
};

/// Represents a parsed DSDL definition.
class Type
{
    friend std::pmr::unordered_map<TypeNameAndVersion, Type> read_namespaces(directories, pmr, ...);
public:
    /// Constructs a default-initialized Object of this Type.
    Object instantiate() const;
    ...
};

using TypeNameAndVersion = std::tuple<std::pmr::string, std::uint8_t, std::uint8_t>;

/// Reads all definitions from the specified namespaces and returns mapping from the full type name
/// and version to its type model.
/// Optionally, the function should cache the results per namespace, with an option to disable the cache.
std::pmr::unordered_map<TypeNameAndVersion, Type> read_namespaces(directories, pmr, ...);
}
