namespace ocvsmd
{

/// The daemon always has the standard file server running.
/// This interface can be used to configure it.
/// It is not possible to stop the server; the closest alternative is to remove all root directories.
/// File server serves Cyphal network 'File' requests by matching the requested path against the list of root directories.
/// The very first match (found file) is served - that is why order of the root entries is important.
class FileServer
{
public:
    // `errno`-like error code (or anything else we might add in the future).
    using Failure = int; // or `cetl::variant<int, ...>`

    /// When the file server handles a request, it will attempt to locate the path relative to each of its root
    /// directories. See Yakut for a hands-on example.
    /// The daemon will internally canonicalize the path and resolve symlinks,
    /// and use real the file system path when matching and serving Cyphal network 'File' requests.
    /// The same path may be added multiple times to avoid interference across different clients.
    /// Currently the path should be a directory (later we might support a direct file as well).
    /// The `back` flag determines whether the path is added to the front or the back of the list.
    /// The changed list of paths is persisted by the daemon (in its configuration; on its exit),
    /// so the list will be automatically restored on the next daemon start.
    /// Returns `cetl::nullopt` on success.
    ///
    virtual cetl::optional<Failure> push_root(const cetl::string_view path, const bool back);

    /// Does nothing if such root does not exist (no error reported).
    /// If such root is listed more than once, only one copy is removed (see `back` param).
    /// The `back` flag determines whether the path is searched from the front or the back of the list.
    /// The flag has no effect if there are no duplicates.
    /// The changed list of paths is persisted by the daemon (in its configuration; on its exit),
    /// so the list will be automatically restored on the next daemon start.
    /// Returns `cetl::nullopt` on success (or if path not found).
    ///
    virtual cetl::optional<Failure> pop_root(const cetl::string_view path, const bool back);

    /// The returned paths are the same as they were added by `push_root`.
    /// The entries are not unique. The order is preserved.
    ///
    virtual cetl::variant<std::vector<std::string>, Failure> list_roots() const;
};

}
