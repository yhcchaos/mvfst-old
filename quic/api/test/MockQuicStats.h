/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#pragma once

#include <folly/io/async/EventBase.h>
#include <folly/portability/GMock.h>

#include <quic/state/QuicTransportStatsCallback.h>

namespace quic {

class MockQuicStats : public QuicTransportStatsCallback {
 public:
  MOCK_METHOD0(onPacketReceived, void());
  MOCK_METHOD0(onDuplicatedPacketReceived, void());
  MOCK_METHOD0(onOutOfOrderPacketReceived, void());
  MOCK_METHOD0(onPacketProcessed, void());
  MOCK_METHOD0(onPacketSent, void());
  MOCK_METHOD0(onPacketRetransmission, void());
  MOCK_METHOD1(onPacketDropped, void(PacketDropReason));
  MOCK_METHOD0(onPacketForwarded, void());
  MOCK_METHOD0(onForwardedPacketReceived, void());
  MOCK_METHOD0(onForwardedPacketProcessed, void());
  MOCK_METHOD0(onNewConnection, void());
  MOCK_METHOD1(onConnectionClose, void(folly::Optional<ConnectionCloseReason>));
  MOCK_METHOD0(onNewQuicStream, void());
  MOCK_METHOD0(onQuicStreamClosed, void());
  MOCK_METHOD0(onQuicStreamReset, void());
  MOCK_METHOD0(onConnFlowControlUpdate, void());
  MOCK_METHOD0(onConnFlowControlBlocked, void());
  MOCK_METHOD0(onStatelessReset, void());
  MOCK_METHOD0(onStreamFlowControlUpdate, void());
  MOCK_METHOD0(onStreamFlowControlBlocked, void());
  MOCK_METHOD0(onCwndBlocked, void());
  MOCK_METHOD0(onPTO, void());
  MOCK_METHOD1(onRead, void(size_t));
  MOCK_METHOD1(onWrite, void(size_t));
  MOCK_METHOD1(onUDPSocketWriteError, void(SocketErrorType));
};

class MockQuicStatsFactory : public QuicTransportStatsCallbackFactory {
 public:
  ~MockQuicStatsFactory() override = default;

  MOCK_METHOD1(
      make,
      std::unique_ptr<QuicTransportStatsCallback>(folly::EventBase*));
};
} // namespace quic
