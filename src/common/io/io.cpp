//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#include "io.hpp"

#include <unistd.h>

namespace ocvsmd
{
namespace common
{
namespace io
{

void OwnedFd::reset() noexcept
{
    if (fd_ >= 0)
    {
        // Do not use `posixSyscallError` here b/c `close` should not be repeated on `EINTR`.
        ::close(fd_);
        fd_ = -1;
    }
}

OwnedFd::~OwnedFd()
{
    reset();
}

}  // namespace io
}  // namespace common
}  // namespace ocvsmd
