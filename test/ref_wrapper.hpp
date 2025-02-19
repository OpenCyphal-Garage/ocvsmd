//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef OCVSMD_REF_WRAPPER_HPP_INCLUDED
#define OCVSMD_REF_WRAPPER_HPP_INCLUDED

namespace ocvsmd
{

template <typename Interface, typename Reference>
struct RefWrapper : Interface
{
    explicit RefWrapper(Reference& reference)
        : reference_{reference}
    {
    }

    RefWrapper(const RefWrapper& other)          = delete;
    RefWrapper(RefWrapper&&) noexcept            = delete;
    RefWrapper& operator=(const RefWrapper&)     = delete;
    RefWrapper& operator=(RefWrapper&&) noexcept = delete;

    virtual ~RefWrapper()
    {
        reference_.deinit();
    }

    Reference& reference()
    {
        return reference_;
    }

private:
    Reference& reference_;

};  // RefWrapper

}  // namespace ocvsmd

#endif  // OCVSMD_REF_WRAPPER_HPP_INCLUDED
