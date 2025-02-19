//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "config.hpp"
#include "engine_helpers.hpp"
#include "file_provider.hpp"
#include "logging.hpp"
#include "svc/file_server/list_roots_spec.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/presentation/server.hpp>

#include <uavcan/file/Error_1_0.hpp>
#include <uavcan/file/GetInfo_0_2.hpp>
#include <uavcan/file/Path_2_0.hpp>
#include <uavcan/file/Read_1_1.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <ios>
#include <memory>
#include <stdlib.h>  // NOLINT ::realpath
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace ocvsmd
{
namespace daemon
{
namespace engine
{
namespace cyphal
{
namespace
{

class FileProviderImpl final : public FileProvider
{
    template <typename T>
    struct SvcSpec : T
    {
        using Server      = libcyphal::presentation::ServiceServer<T>;
        using CallbackArg = typename Server::OnRequestCallback::Arg;
    };
    struct Svc
    {
        using Read    = SvcSpec<uavcan::file::Read_1_1>;
        using GetInfo = SvcSpec<uavcan::file::GetInfo_0_2>;

    };  // Svc

public:
    static Ptr make(cetl::pmr::memory_resource&            memory,
                    libcyphal::presentation::Presentation& presentation,
                    Config::Ptr                            config)
    {
        auto read_srv     = makeServer<Svc::Read>("Read", presentation);
        auto get_info_srv = makeServer<Svc::GetInfo>("GetInfo", presentation);
        if (!read_srv || !get_info_srv)
        {
            return nullptr;
        }
        return std::make_unique<FileProviderImpl>(memory,
                                                  std::move(config),
                                                  std::move(*read_srv),
                                                  std::move(*get_info_srv));
    }

    FileProviderImpl(cetl::pmr::memory_resource& memory,
                     Config::Ptr                 config,
                     Svc::Read::Server&&         read_srv,
                     Svc::GetInfo::Server&&      get_info_srv)
        : memory_{memory}
        , config_{std::move(config)}
        , read_srv_{std::move(read_srv)}
        , get_info_srv_{std::move(get_info_srv)}
    {
        using ListRootsSpec       = common::svc::file_server::ListRootsSpec;
        constexpr auto MaxRootLen = ListRootsSpec::Response::_traits_::TypeOf::item::_traits_::ArrayCapacity::path;

        logger_->trace("FileProviderImpl().");

        roots_ = config_->getFileServerRoots();
        logger_->debug("There are {} file server roots.", roots_.size());
        for (std::size_t i = 0; i < roots_.size(); ++i)
        {
            if (const auto real_root = canonicalizePath(roots_[i]))
            {
                if (roots_[i].size() <= MaxRootLen)
                {
                    logger_->trace("{:4} '{}' → '{}'", i, roots_[i], *real_root);
                }
                else
                {
                    logger_->warn("{:4} 🟡 too long '{}' → '{}'", i, roots_[i], *real_root);
                }
            }
            else
            {
                logger_->warn("{:4} '{}' → ❌ not found!", i, roots_[i]);
            }
        }

        setupOnRequestCallback<Svc::GetInfo>(get_info_srv_, [this](const auto& arg) {
            //
            return serveGetInfoRequest(arg);
        });
        setupOnRequestCallback<Svc::Read>(read_srv_, [this](const auto& arg) {
            //
            return serveReadRequest(arg);
        });
    }

    FileProviderImpl(const FileProviderImpl&)                = delete;
    FileProviderImpl(FileProviderImpl&&) noexcept            = delete;
    FileProviderImpl& operator=(const FileProviderImpl&)     = delete;
    FileProviderImpl& operator=(FileProviderImpl&&) noexcept = delete;

    ~FileProviderImpl() override
    {
        logger_->trace("~FileProviderImpl.");
    }

    // FileProvider

    /// Gets the list of roots that the file provider is serving.
    ///
    auto getListOfRoots() const -> const std::vector<std::string>& override
    {
        return roots_;
    }

    void popRoot(const std::string& path, const bool back) override
    {
        decltype(roots_.end()) it;
        if (back)
        {
            // Search in reverse order, and convert back to forward iterator.
            //
            const auto rev_it = std::find(roots_.rbegin(), roots_.rend(), path);
            it                = rev_it.base() - 1;
        }
        else
        {
            // Search in forward order.
            it = std::find(roots_.begin(), roots_.end(), path);
        }

        if (it != roots_.end())
        {
            roots_.erase(it);
            config_->setFileServerRoots(roots_);
        }
    }

    void pushRoot(const std::string& path, const bool back) override
    {
        roots_.insert(back ? roots_.end() : roots_.begin(), path);
        config_->setFileServerRoots(roots_);
    }

private:
    using Presentation = libcyphal::presentation::Presentation;

    template <typename Service>
    static auto makeServer(const cetl::string_view role,
                           Presentation&           presentation) -> cetl::optional<typename Service::Server>
    {
        auto maybe_server = presentation.makeServer<Service>();
        if (const auto* const failure = cetl::get_if<Presentation::MakeFailure>(&maybe_server))
        {
            const auto err = failureToErrorCode(*failure);
            spdlog::error("Failed to make '{}' server (err={}).", role, err);
            return cetl::nullopt;
        }
        return cetl::get<typename Service::Server>(std::move(maybe_server));
    }

    template <typename Service, typename Handler>
    void setupOnRequestCallback(typename Service::Server& server, Handler&& handler)
    {
        server.setOnRequestCallback([handle = std::forward<Handler>(handler)](const auto& arg, auto& continuation) {
            //
            constexpr auto timeout = std::chrono::milliseconds{100};
            continuation(arg.approx_now + timeout, handle(arg));
        });
    }

    static std::string stringFrom(const uavcan::file::Path_2_0& file_path)
    {
        const auto* const path_data = reinterpret_cast<const char*>(file_path.path.data());  // NOLINT
        return std::string{path_data, file_path.path.size()};
    }

    static auto convertErrorCode(const int code) -> uavcan::file::Error_1_0::_traits_::TypeOf::value
    {
        switch (code)
        {
        case EIO:
        case EPERM:
            return uavcan::file::Error_1_0::IO_ERROR;
        case ENOENT:
            return uavcan::file::Error_1_0::NOT_FOUND;
        case EISDIR:
            return uavcan::file::Error_1_0::IS_DIRECTORY;
        case ENOSPC:
            return uavcan::file::Error_1_0::OUT_OF_SPACE;
        case EACCES:
            return uavcan::file::Error_1_0::ACCESS_DENIED;
        case EINVAL:
            return uavcan::file::Error_1_0::INVALID_VALUE;
        case ENOTSUP:
            return uavcan::file::Error_1_0::NOT_SUPPORTED;
        case E2BIG:
            return uavcan::file::Error_1_0::FILE_TOO_LARGE;
        default:
            return uavcan::file::Error_1_0::UNKNOWN_ERROR;
        }
    }

    static cetl::optional<std::string> canonicalizePath(const std::string& path)
    {
        char* const resolved_path = ::realpath(path.c_str(), nullptr);
        if (resolved_path == nullptr)
        {
            return cetl::nullopt;
        }

        std::string result{resolved_path};
        ::free(resolved_path);  // NOLINT

        return result;
    }

    static cetl::optional<std::string> buildAndValidateRootWithPath(const std::string& root, const std::string& file)
    {
        if (const auto root_path = canonicalizePath(root))
        {
            if (auto sure_file_path = canonicalizePath(root + "/" + file))
            {
                auto& file_path = *sure_file_path;

                // This is a security check to ensure that the resolved path is UNDER the real root path.
                // The last `/` check is important to ensure that the `root_len` path is a directory.
                //
                const auto root_len = root_path->size();
                if ((0 == file_path.compare(0, root_len, *root_path)) && (file_path[root_len] == '/'))
                {
                    return sure_file_path;
                }
            }
        }
        return cetl::nullopt;
    }

    cetl::optional<std::pair<std::string, struct stat>> findFirstValidFile(const std::string& request_path) const
    {
        for (const auto& root : roots_)
        {
            if (const auto real_path = buildAndValidateRootWithPath(root, request_path))
            {
                // As "best effort" strategy, we skip anything we can't even `stat`.
                //
                struct stat file_stat
                {};
                if (::stat(real_path->c_str(), &file_stat) == 0)
                {
                    return std::make_pair(*real_path, file_stat);
                }
            }
        }
        return cetl::nullopt;
    }

    Svc::GetInfo::Response serveGetInfoRequest(const Svc::GetInfo::CallbackArg& arg) const
    {
        Svc::GetInfo::Response response{&memory_};

        // Find the first valid file candidate in the list of roots.
        //
        const auto request_path  = stringFrom(arg.request.path);
        const auto path_and_stat = findFirstValidFile(request_path);
        if (!path_and_stat)
        {
            logger_->warn(  //
                "'GetInfo' file not found (node={}, path='{}').",
                arg.metadata.remote_node_id,
                request_path);

            response._error.value = uavcan::file::Error_1_0::NOT_FOUND;
            return response;
        }
        const auto& file_path = path_and_stat->first;
        const auto& file_stat = path_and_stat->second;

        logger_->debug(  //
            "'GetInfo' found file info (node={}, path='{}', size={}, real='{}').",
            arg.metadata.remote_node_id,
            request_path,
            file_stat.st_size,
            file_path);

        response._error.value                        = uavcan::file::Error_1_0::OK;
        response.size                                = file_stat.st_size;
        response.unix_timestamp_of_last_modification = file_stat.st_mtime;
        response.is_file_not_directory               = !S_ISDIR(file_stat.st_mode);
        response.is_link                             = false;  // `::realpath()` resolved all possible links
        response.is_readable                         = (0 == ::access(file_path.c_str(), R_OK));
        response.is_writeable                        = (0 == ::access(file_path.c_str(), W_OK));
        return response;
    }

    Svc::Read::Response serveReadRequest(const Svc::Read::CallbackArg& arg) const
    {
        using DataType             = Svc::Read::Response::_traits_::TypeOf::data;
        constexpr auto MaxDataSize = DataType::_traits_::ArrayCapacity::value;

        Svc::Read::Response response{&memory_};
        response._error.value = uavcan::file::Error_1_0::OK;

        // Find the first valid file candidate in the list of roots.
        //
        const auto request_path  = stringFrom(arg.request.path);
        const auto path_and_stat = findFirstValidFile(request_path);
        if (!path_and_stat)
        {
            logger_->warn(  //
                "'Read' file not found (node={}, path='{}', off=0x{:X}).",
                arg.metadata.remote_node_id,
                request_path,
                arg.request.offset);

            response._error.value = uavcan::file::Error_1_0::NOT_FOUND;
            return response;
        }
        const auto& file_path = path_and_stat->first;
        const auto& file_stat = path_and_stat->second;

        // Don't allow reading beyond the end of the file.
        //
        if (arg.request.offset >= file_stat.st_size)
        {
            logger_->debug(  //
                "'Read' eof (node={}, path='{}', off=0x{:X}, eof=0x{:X}, real='{}').",
                arg.metadata.remote_node_id,
                request_path,
                arg.request.offset,
                file_stat.st_size,
                file_path);

            return response;
        }

        // Read the next chunk of data at the given offset.
        //
        // Currently, we don't have any "state" in this file provider,
        // so we match, validate and re-open the file every time from scratch.
        // OS normally heavily caches file system entries and data anyway.
        // If in the future we need to optimize this, we can add a configurable "soft" caching here
        // (f.e. LRU + flush on change of roots and maybe on some expiration period; refresh on 'GetInfo').
        //
        auto&      buffer        = response.data.value;
        const auto bytes_to_read = std::min<std::size_t>(file_stat.st_size - arg.request.offset, MaxDataSize);
        buffer.resize(bytes_to_read);
        try
        {
            std::ifstream file{file_path.c_str(), std::ios::binary};
            file.exceptions(std::ifstream::failbit | std::ifstream::badbit);

            file.seekg(static_cast<std::streamoff>(arg.request.offset));
            file.read(reinterpret_cast<char*>(buffer.data()), bytes_to_read);  // NOLINT

            // The read count should be the same as `bytes_to_read` but let's be sure.
            // The `resize` method does nothing if the new size is the same as the old one.
            buffer.resize(file.gcount());
            CETL_DEBUG_ASSERT(buffer.size() <= bytes_to_read, "");

            // Limit the log output to the first and/or last request in the sequence. Otherwise,
            // the log will be flooded with almost the same messages - not useful even under trace level.
            // It's still possible to do the flooding (if one keeps reading the first/last chunk over and over),
            // but it's an edge case (anyway, we have a log file limit and rotation policy in place).
            //
            if ((arg.request.offset + buffer.size()) >= file_stat.st_size)  // last?
            {
                logger_->debug(  //
                    "'Read' last (node={}, path='{}', off=0x{:X}, eof=0x{:X}, real='{}').",
                    arg.metadata.remote_node_id,
                    request_path,
                    arg.request.offset,
                    file_stat.st_size,
                    file_path);
            }
            else if (arg.request.offset == 0)  // first?
            {
                logger_->debug(  //
                    "'Read' first (node={}, path='{}', eof=0x{:X}, real='{}')…",
                    arg.metadata.remote_node_id,
                    request_path,
                    file_stat.st_size,
                    file_path);
            }

        } catch (const std::ios_base::failure& ex)
        {
            logger_->warn(  //
                "'Read' failed (node={}, path='{}', off=0x{:X}, eof=0x{:X}, real='{}', err={}): {}.",
                arg.metadata.remote_node_id,
                request_path,
                arg.request.offset,
                file_stat.st_size,
                file_path,
                ex.code().value(),
                ex.what());

            buffer.clear();  // No need to send any garbage data.
            response._error.value = convertErrorCode(ex.code().value());
        }
        return response;
    }

    cetl::pmr::memory_resource& memory_;
    Config::Ptr                 config_;
    Svc::Read::Server           read_srv_;
    Svc::GetInfo::Server        get_info_srv_;
    common::LoggerPtr           logger_{common::getLogger("engine")};
    std::vector<std::string>    roots_;

};  // FileProviderImpl

}  // namespace

FileProvider::Ptr FileProvider::make(cetl::pmr::memory_resource&            memory,
                                     libcyphal::presentation::Presentation& presentation,
                                     Config::Ptr                            config)
{
    return FileProviderImpl::make(memory, presentation, std::move(config));
}

}  // namespace cyphal
}  // namespace engine
}  // namespace daemon
}  // namespace ocvsmd
