//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_COMMON_IO_HPP_INCLUDED
#define OCVSMD_COMMON_IO_HPP_INCLUDED

#include <utility>

namespace ocvsmd
{
namespace common
{
namespace io
{

/// RAII wrapper for a file descriptor.
///
class OwnedFd final
{
public:
    OwnedFd()
        : fd_{-1}
    {
    }

    explicit OwnedFd(const int fd)
        : fd_{fd}
    {
    }

    OwnedFd(OwnedFd&& other) noexcept
        : fd_{std::exchange(other.fd_, -1)}
    {
    }

    OwnedFd& operator=(OwnedFd&& other) noexcept
    {
        const OwnedFd old{std::move(*this)};
        fd_ = std::exchange(other.fd_, -1);
        return *this;
    }

    OwnedFd& operator=(std::nullptr_t)
    {
        const OwnedFd old{std::move(*this)};
        return *this;
    }

    // Disallow copy.
    OwnedFd(const OwnedFd&)            = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;

    int get() const
    {
        return fd_;
    }

    void reset() noexcept;

    ~OwnedFd();

private:
    int fd_;

};  // OwnedFd

}  // namespace io
}  // namespace common
}  // namespace ocvsmd

#endif  // OCVSMD_COMMON_IO_HPP_INCLUDED
