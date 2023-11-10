/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#pragma once

#include <folly/portability/GMock.h>

#include <quic/api/QuicSocket.h>

namespace quic {

class MockQuicSocket : public QuicSocket {
 public:
  using SharedBuf = std::shared_ptr<folly::IOBuf>;

  MockQuicSocket(folly::EventBase* /*eventBase*/, ConnectionCallback& cb)
      : cb_(&cb) {}

  MOCK_CONST_METHOD0(good, bool());
  MOCK_CONST_METHOD0(replaySafe, bool());
  MOCK_CONST_METHOD0(error, bool());
  MOCK_METHOD1(
      close,
      void(folly::Optional<std::pair<QuicErrorCode, std::string>>));
  MOCK_METHOD0(closeGracefully, void());
  MOCK_METHOD1(
      closeNow,
      void(folly::Optional<std::pair<QuicErrorCode, std::string>>));
  MOCK_CONST_METHOD0(
      getClientConnectionId,
      folly::Optional<quic::ConnectionId>());
  MOCK_CONST_METHOD0(getTransportSettings, const TransportSettings&());
  MOCK_CONST_METHOD0(
      getServerConnectionId,
      folly::Optional<quic::ConnectionId>());
  MOCK_CONST_METHOD0(
      getClientChosenDestConnectionId,
      folly::Optional<quic::ConnectionId>());
  MOCK_CONST_METHOD0(getPeerAddress, const folly::SocketAddress&());
  MOCK_CONST_METHOD0(getOriginalPeerAddress, const folly::SocketAddress&());
  MOCK_CONST_METHOD0(getLocalAddress, const folly::SocketAddress&());
  MOCK_CONST_METHOD0(getEventBase, folly::EventBase*());
  MOCK_CONST_METHOD1(
      getStreamReadOffset,
      folly::Expected<size_t, LocalErrorCode>(StreamId));
  MOCK_CONST_METHOD1(
      getStreamWriteOffset,
      folly::Expected<size_t, LocalErrorCode>(StreamId));
  MOCK_CONST_METHOD1(
      getStreamWriteBufferedBytes,
      folly::Expected<size_t, LocalErrorCode>(StreamId));
  MOCK_CONST_METHOD0(getTransportInfo, QuicSocket::TransportInfo());
  MOCK_CONST_METHOD1(
      getStreamTransportInfo,
      folly::Expected<QuicSocket::StreamTransportInfo, LocalErrorCode>(
          StreamId));
  MOCK_CONST_METHOD0(getAppProtocol, folly::Optional<std::string>());
  MOCK_METHOD2(setReceiveWindow, void(StreamId, size_t));
  MOCK_METHOD3(setSendBuffer, void(StreamId, size_t, size_t));
  MOCK_CONST_METHOD0(getConnectionBufferAvailable, uint64_t());
  MOCK_CONST_METHOD0(
      getConnectionFlowControl,
      folly::Expected<FlowControlState, LocalErrorCode>());
  MOCK_CONST_METHOD1(
      getStreamFlowControl,
      folly::Expected<FlowControlState, LocalErrorCode>(StreamId));
  MOCK_METHOD0(unsetAllReadCallbacks, void());
  MOCK_METHOD0(unsetAllPeekCallbacks, void());
  MOCK_METHOD0(unsetAllDeliveryCallbacks, void());
  MOCK_METHOD1(cancelDeliveryCallbacksForStream, void(StreamId));
  MOCK_METHOD2(
      cancelDeliveryCallbacksForStream,
      void(StreamId, uint64_t offset));
  MOCK_METHOD1(
      setConnectionFlowControlWindow,
      folly::Expected<folly::Unit, LocalErrorCode>(uint64_t));
  MOCK_METHOD2(
      setStreamFlowControlWindow,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId, uint64_t));
  MOCK_METHOD1(setTransportSettings, void(TransportSettings));
  MOCK_CONST_METHOD0(isPartiallyReliableTransport, bool());
  MOCK_METHOD2(
      setReadCallback,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId, ReadCallback*));
  MOCK_METHOD1(setConnectionCallback, void(ConnectionCallback*));
  void setEarlyDataAppParamsFunctions(
      folly::Function<bool(const folly::Optional<std::string>&, const Buf&)
                          const> validator,
      folly::Function<Buf()> getter) override {
    earlyDataAppParamsValidator_ = std::move(validator);
    earlyDataAppParamsGetter_ = std::move(getter);
  }
  MOCK_METHOD1(
      pauseRead,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId));
  MOCK_METHOD1(
      resumeRead,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId));
  MOCK_METHOD2(
      stopSending,
      folly::Expected<folly::Unit, LocalErrorCode>(
          StreamId,
          ApplicationErrorCode));
  folly::Expected<std::pair<Buf, bool>, LocalErrorCode> read(
      StreamId id,
      size_t maxRead) override {
    auto res = readNaked(id, maxRead);
    if (res.hasError()) {
      return folly::makeUnexpected(res.error());
    } else {
      return std::pair<Buf, bool>(Buf(res.value().first), res.value().second);
    }
  }
  using ReadResult =
      folly::Expected<std::pair<folly::IOBuf*, bool>, LocalErrorCode>;
  MOCK_METHOD2(readNaked, ReadResult(StreamId, size_t));
  MOCK_METHOD1(
      createBidirectionalStream,
      folly::Expected<StreamId, LocalErrorCode>(bool));
  MOCK_METHOD1(
      createUnidirectionalStream,
      folly::Expected<StreamId, LocalErrorCode>(bool));
  MOCK_CONST_METHOD0(getNumOpenableBidirectionalStreams, uint64_t());
  MOCK_CONST_METHOD0(getNumOpenableUnidirectionalStreams, uint64_t());
  GMOCK_METHOD1_(, noexcept, , isClientStream, bool(StreamId));
  GMOCK_METHOD1_(, noexcept, , isServerStream, bool(StreamId));
  GMOCK_METHOD1_(, noexcept, , isBidirectionalStream, bool(StreamId));
  GMOCK_METHOD1_(, noexcept, , isUnidirectionalStream, bool(StreamId));
  MOCK_METHOD1(
      notifyPendingWriteOnConnection,
      folly::Expected<folly::Unit, LocalErrorCode>(WriteCallback*));
  MOCK_METHOD2(
      notifyPendingWriteOnStream,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId, WriteCallback*));
  folly::Expected<Buf, LocalErrorCode> writeChain(
      StreamId id,
      Buf data,
      bool eof,
      bool cork,
      DeliveryCallback* cb) override {
    SharedBuf sharedData(data.release());
    auto res = writeChain(id, sharedData, eof, cork, cb);
    if (res.hasError()) {
      return folly::makeUnexpected(res.error());
    } else {
      return Buf(res.value());
    }
  }
  using WriteResult = folly::Expected<folly::IOBuf*, LocalErrorCode>;
  MOCK_METHOD5(
      writeChain,
      WriteResult(StreamId, SharedBuf, bool, bool, DeliveryCallback*));
  MOCK_METHOD3(
      registerDeliveryCallback,
      folly::Expected<folly::Unit, LocalErrorCode>(
          StreamId,
          uint64_t,
          DeliveryCallback*));

  MOCK_METHOD1(shutdownWrite, folly::Optional<LocalErrorCode>(StreamId));
  MOCK_METHOD2(
      resetStream,
      folly::Expected<folly::Unit, LocalErrorCode>(
          StreamId,
          ApplicationErrorCode));
  MOCK_METHOD2(
      maybeResetStreamFromReadError,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId, QuicErrorCode));
  MOCK_METHOD2(sendPing, void(PingCallback*, std::chrono::milliseconds));
  MOCK_CONST_METHOD0(getState, const QuicConnectionStateBase*());
  MOCK_METHOD0(isDetachable, bool());
  MOCK_METHOD1(attachEventBase, void(folly::EventBase*));
  MOCK_METHOD0(detachEventBase, void());
  MOCK_METHOD1(setControlStream, folly::Optional<LocalErrorCode>(StreamId));

  MOCK_METHOD2(
      setPeekCallback,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId, PeekCallback*));

  MOCK_METHOD1(
      pausePeek,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId));
  MOCK_METHOD1(
      resumePeek,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId));

  MOCK_METHOD2(
      peek,
      folly::Expected<folly::Unit, LocalErrorCode>(
          StreamId,
          const folly::Function<
              void(StreamId, const folly::Range<PeekIterator>&) const>&));

  MOCK_METHOD3(
      consume,
      folly::Expected<
          folly::Unit,
          std::pair<LocalErrorCode, folly::Optional<uint64_t>>>(
          StreamId,
          uint64_t,
          size_t));
  MOCK_METHOD2(
      consume,
      folly::Expected<folly::Unit, LocalErrorCode>(StreamId, size_t));

  MOCK_METHOD2(
      setDataExpiredCallback,
      folly::Expected<folly::Unit, LocalErrorCode>(
          StreamId,
          DataExpiredCallback*));

  MOCK_METHOD2(
      sendDataExpired,
      folly::Expected<folly::Optional<uint64_t>, LocalErrorCode>(
          StreamId,
          uint64_t offset));

  MOCK_METHOD2(
      setDataRejectedCallback,
      folly::Expected<folly::Unit, LocalErrorCode>(
          StreamId,
          DataRejectedCallback*));

  MOCK_METHOD2(
      sendDataRejected,
      folly::Expected<folly::Optional<uint64_t>, LocalErrorCode>(
          StreamId,
          uint64_t offset));

  MOCK_METHOD1(setCongestionControl, void(CongestionControlType));

  ConnectionCallback* cb_;

  folly::Function<bool(const folly::Optional<std::string>&, const Buf&)>
      earlyDataAppParamsValidator_;
  folly::Function<Buf()> earlyDataAppParamsGetter_;
};
} // namespace quic
