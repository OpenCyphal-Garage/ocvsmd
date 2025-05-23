//
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: MIT
//

#ifndef LIBCYPHAL_TRANSPORT_MSG_SESSIONS_MOCK_HPP_INCLUDED
#define LIBCYPHAL_TRANSPORT_MSG_SESSIONS_MOCK_HPP_INCLUDED

#include "unique_ptr_ref_wrapper.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/transport/msg_sessions.hpp>
#include <libcyphal/transport/types.hpp>
#include <libcyphal/types.hpp>

#include <gmock/gmock.h>

#include <utility>

namespace libcyphal
{
namespace transport
{

class MessageRxSessionMock : public IMessageRxSession
{
public:
    struct RefWrapper final : UniquePtrRefWrapper<IMessageRxSession, MessageRxSessionMock, RefWrapper>
    {
        using UniquePtrRefWrapper::UniquePtrRefWrapper;

        // MARK: IMessageRxSession

        void setTransferIdTimeout(const Duration timeout) override
        {
            reference().setTransferIdTimeout(timeout);
        }
        MessageRxParams getParams() const noexcept override
        {
            return reference().getParams();
        }
        cetl::optional<MessageRxTransfer> receive() override
        {
            return reference().receive();
        }
        void setOnReceiveCallback(OnReceiveCallback::Function&& function) override
        {
            reference().setOnReceiveCallback(std::move(function));
        }

    };  // RefWrapper

    MessageRxSessionMock()          = default;
    virtual ~MessageRxSessionMock() = default;

    MessageRxSessionMock(const MessageRxSessionMock&)                = delete;
    MessageRxSessionMock(MessageRxSessionMock&&) noexcept            = delete;
    MessageRxSessionMock& operator=(const MessageRxSessionMock&)     = delete;
    MessageRxSessionMock& operator=(MessageRxSessionMock&&) noexcept = delete;

    MOCK_METHOD(void, setTransferIdTimeout, (const Duration timeout), (override));
    MOCK_METHOD(MessageRxParams, getParams, (), (const, noexcept, override));
    MOCK_METHOD(cetl::optional<MessageRxTransfer>, receive, (), (override));
    MOCK_METHOD(void, setOnReceiveCallback, (OnReceiveCallback::Function&&), (override));
    MOCK_METHOD(void, deinit, (), (noexcept));  // NOLINT(*-exception-escape)

};  // MessageRxSessionMock

// MARK: -

class MessageTxSessionMock : public IMessageTxSession
{
public:
    struct RefWrapper final : UniquePtrRefWrapper<IMessageTxSession, MessageTxSessionMock, RefWrapper>
    {
        using UniquePtrRefWrapper::UniquePtrRefWrapper;

        // MARK: IMessageTxSession

        MessageTxParams getParams() const noexcept override
        {
            return reference().getParams();
        }
        cetl::optional<AnyFailure> send(const TransferTxMetadata& metadata,
                                        const PayloadFragments    payload_fragments) override
        {
            return reference().send(metadata, payload_fragments);
        }

    };  // RefWrapper

    MessageTxSessionMock()          = default;
    virtual ~MessageTxSessionMock() = default;

    MessageTxSessionMock(const MessageTxSessionMock&)                = delete;
    MessageTxSessionMock(MessageTxSessionMock&&) noexcept            = delete;
    MessageTxSessionMock& operator=(const MessageTxSessionMock&)     = delete;
    MessageTxSessionMock& operator=(MessageTxSessionMock&&) noexcept = delete;

    MOCK_METHOD(MessageTxParams, getParams, (), (const, noexcept, override));
    MOCK_METHOD(cetl::optional<AnyFailure>,
                send,
                (const TransferTxMetadata& metadata, const PayloadFragments payload_fragments),
                (override));
    MOCK_METHOD(void, deinit, (), ());

};  // MessageTxSessionMock

}  // namespace transport
}  // namespace libcyphal

#endif  // LIBCYPHAL_TRANSPORT_MSG_SESSIONS_MOCK_HPP_INCLUDED
