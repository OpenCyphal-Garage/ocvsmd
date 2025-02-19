//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef LIBCYPHAL_UNIQUE_PTR_REF_WRAPPER_HPP_INCLUDED
#define LIBCYPHAL_UNIQUE_PTR_REF_WRAPPER_HPP_INCLUDED

#include <libcyphal/types.hpp>

namespace libcyphal
{

template <typename Interface, typename Reference, typename DerivedWrapper>
struct UniquePtrRefWrapper : Interface
{
    struct Spec : detail::UniquePtrSpec<Interface, DerivedWrapper>
    {};

    explicit UniquePtrRefWrapper(Reference& reference)
        : reference_{reference}
    {
    }

    UniquePtrRefWrapper(const UniquePtrRefWrapper& other)          = delete;
    UniquePtrRefWrapper(UniquePtrRefWrapper&&) noexcept            = delete;
    UniquePtrRefWrapper& operator=(const UniquePtrRefWrapper&)     = delete;
    UniquePtrRefWrapper& operator=(UniquePtrRefWrapper&&) noexcept = delete;

    virtual ~UniquePtrRefWrapper()
    {
        reference_.deinit();
    }

    Reference& reference() const
    {
        return reference_;
    }

private:
    Reference& reference_;

};  // UniquePtrRefWrapper

}  // namespace libcyphal

#endif  // LIBCYPHAL_UNIQUE_PTR_REF_WRAPPER_HPP_INCLUDED
