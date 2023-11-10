/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#pragma once

#include <folly/dynamic.h>
#include <quic/codec/Types.h>
#include <quic/logging/BaseQLogger.h>
#include <quic/logging/QLoggerConstants.h>
#include <quic/logging/QLoggerTypes.h>

namespace quic {

class FileQLogger : public BaseQLogger {
 public:
  std::vector<std::unique_ptr<QLogEvent>> logs;
  FileQLogger(
      VantagePoint vantagePointIn,
      std::string protocolTypeIn = kHTTP3ProtocolType)
      : BaseQLogger(vantagePointIn, std::move(protocolTypeIn)) {}

  ~FileQLogger() override = default;
  void addPacket(const RegularQuicPacket& regularPacket, uint64_t packetSize)
      override;
  void addPacket(
      const VersionNegotiationPacket& versionPacket,
      uint64_t packetSize,
      bool isPacketRecvd) override;
  void addPacket(const RegularQuicWritePacket& writePacket, uint64_t packetSize)
      override;
  void addConnectionClose(
      std::string error,
      std::string reason,
      bool drainConnection,
      bool sendCloseImmediately) override;
  void addTransportSummary(
      uint64_t totalBytesSent,
      uint64_t totalBytesRecvd,
      uint64_t sumCurWriteOffset,
      uint64_t sumMaxObservedOffset,
      uint64_t sumCurStreamBufferLen,
      uint64_t totalBytesRetransmitted,
      uint64_t totalStreamBytesCloned,
      uint64_t totalBytesCloned,
      uint64_t totalCryptoDataWritten,
      uint64_t totalCryptoDataRecvd) override;
  void addCongestionMetricUpdate(
      uint64_t bytesInFlight,
      uint64_t currentCwnd,
      std::string congestionEvent,
      std::string state = "",
      std::string recoveryState = "") override;
  void addPacingMetricUpdate(
      uint64_t pacingBurstSizeIn,
      std::chrono::microseconds pacingIntervalIn) override;
  void addPacingObservation(
      std::string actual,
      std::string expected,
      std::string conclusion) override;
  void addBandwidthEstUpdate(uint64_t bytes, std::chrono::microseconds interval)
      override;
  void addAppLimitedUpdate() override;
  void addAppUnlimitedUpdate() override;
  void addAppIdleUpdate(std::string idleEvent, bool idle) override;
  void addPacketDrop(size_t packetSize, std::string dropReasonIn) override;
  void addDatagramReceived(uint64_t dataLen) override;
  void addLossAlarm(
      PacketNum largestSent,
      uint64_t alarmCount,
      uint64_t outstandingPackets,
      std::string type) override;
  void addPacketsLost(
      PacketNum largestLostPacketNum,
      uint64_t lostBytes,
      uint64_t lostPackets) override;
  void addTransportStateUpdate(std::string update) override;
  void addPacketBuffered(
      PacketNum packetNum,
      ProtectionType protectionType,
      uint64_t packetSize) override;
  void addMetricUpdate(
      std::chrono::microseconds latestRtt,
      std::chrono::microseconds mrtt,
      std::chrono::microseconds srtt,
      std::chrono::microseconds ackDelay) override;
  void addStreamStateUpdate(
      StreamId id,
      std::string update,
      folly::Optional<std::chrono::milliseconds> timeSinceStreamCreation)
      override;
  virtual void addConnectionMigrationUpdate(bool intentionalMigration) override;
  virtual void addPathValidationEvent(bool success) override;

  void outputLogsToFile(const std::string& path, bool prettyJson);
  folly::dynamic toDynamic() const;
};
} // namespace quic
