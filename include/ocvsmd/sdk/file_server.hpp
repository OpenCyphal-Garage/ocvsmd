//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_SDK_FILE_SERVER_HPP_INCLUDED
#define OCVSMD_SDK_FILE_SERVER_HPP_INCLUDED

#include "execution.hpp"

#include <cetl/pf17/cetlpf.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace ocvsmd
{
namespace sdk
{

/// Defines client side interface of the OCVSMD File Server component.
///
class FileServer
{
public:
    /// Defines the shared pointer type for the interface.
    ///
    using Ptr = std::shared_ptr<FileServer>;

    // No copy/move semantics.
    FileServer(FileServer&&)                 = delete;
    FileServer(const FileServer&)            = delete;
    FileServer& operator=(FileServer&&)      = delete;
    FileServer& operator=(const FileServer&) = delete;

    virtual ~FileServer() = default;

    /// Makes async sender which emits a current list of the File Server root paths.
    ///
    /// @return An execution sender which emits the async result of the operation.
    ///         The returned paths are the same values as they were added by `pushRoot`.
    ///         The entries are not unique, and the order is preserved.
    ///
    struct ListRoots final
    {
        using Success = std::vector<std::string>;
        using Failure = int;  // `errno`-like error code
        using Result  = cetl::variant<Success, Failure>;
    };
    virtual SenderOf<ListRoots::Result>::Ptr listRoots() = 0;

    /// Does nothing if such root path does not exist (no error reported).
    /// If such a path is listed more than once, only one copy is removed.
    /// The `back` flag determines whether the path is searched from the front or the back of the list.
    /// The flag has no effect if there are no duplicates.
    /// The changed list of paths is persisted by the daemon (in its configuration; on its exit),
    /// so the list will be automatically restored on the next daemon start.
    ///
    struct PopRoot final
    {
        using Success = cetl::monostate;  // like `void`
        using Failure = int;              // `errno`-like error code
        using Result  = cetl::variant<Success, Failure>;
    };
    /// Removes a root directory from the list of directories that the file server will serve.
    ///
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<PopRoot::Result>::Ptr popRoot(const std::string& path, const bool back) = 0;

    /// When the file server handles a request, it will attempt to locate the path relative to each of its root
    /// directories. See Yakut for a hands-on example.
    /// The `path` could be a relative or an absolute path. In case of a relative path
    /// (without leading `/`), the daemon will resolve it against its current working directory.
    /// The daemon will internally canonicalize the path and resolve symlinks,
    /// and use real the file system path when matching and serving Cyphal network 'File' requests.
    /// The same path may be added multiple times to avoid interference across different clients.
    /// Currently, the path should be a directory (later we might support a direct file as well).
    /// The `back` flag determines whether the path is added to the front or the back of the list.
    /// The changed list of paths is persisted by the daemon (in its configuration; on its exit),
    /// so the list will be automatically restored on the next daemon start.
    ///
    struct PushRoot final
    {
        using Success = cetl::monostate;  // like `void`
        using Failure = int;              // `errno`-like error code
        using Result  = cetl::variant<Success, Failure>;
    };
    /// Adds a new root directory to the list of directories that the file server will serve.
    ///
    /// @return An execution sender which emits the async result of the operation.
    ///
    virtual SenderOf<PushRoot::Result>::Ptr pushRoot(const std::string& path, const bool back) = 0;

protected:
    FileServer() = default;

};  // FileServer

}  // namespace sdk
}  // namespace ocvsmd

#endif  // OCVSMD_SDK_FILE_SERVER_HPP_INCLUDED
