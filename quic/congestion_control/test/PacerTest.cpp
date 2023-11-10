/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#include <quic/congestion_control/Pacer.h>
#include <folly/portability/GTest.h>

using namespace testing;

namespace quic {
namespace test {

namespace {
void consumeTokensHelper(Pacer& pacer, size_t tokensToConsume) {
  for (size_t i = 0; i < tokensToConsume; i++) {
    pacer.onPacketSent();
  }
}
} // namespace

class PacerTest : public Test {
 public:
  void SetUp() override {
    conn.transportSettings.pacingTimerTickInterval = 1us;
  }

 protected:
  QuicConnectionStateBase conn{QuicNodeType::Client};
  DefaultPacer pacer{conn, conn.transportSettings.minCwndInMss};
};

TEST_F(PacerTest, WriteBeforeScheduled) {
  EXPECT_EQ(
      conn.transportSettings.writeConnectionDataPacketsLimit,
      pacer.updateAndGetWriteBatchSize(Clock::now()));
  EXPECT_EQ(0us, pacer.getTimeUntilNextWrite());
}

TEST_F(PacerTest, RateCalculator) {
  pacer.setPacingRateCalculator([](const QuicConnectionStateBase&,
                                   uint64_t,
                                   uint64_t,
                                   std::chrono::microseconds) {
    return PacingRate::Builder().setInterval(1234us).setBurstSize(4321).build();
  });
  pacer.refreshPacingRate(200000, 200us);
  EXPECT_EQ(0us, pacer.getTimeUntilNextWrite());
  EXPECT_EQ(
      4321 + conn.transportSettings.writeConnectionDataPacketsLimit,
      pacer.updateAndGetWriteBatchSize(Clock::now()));
  consumeTokensHelper(
      pacer, 4321 + conn.transportSettings.writeConnectionDataPacketsLimit);
  EXPECT_EQ(1234us, pacer.getTimeUntilNextWrite());
}

TEST_F(PacerTest, CompensateTimerDrift) {
  pacer.setPacingRateCalculator([](const QuicConnectionStateBase&,
                                   uint64_t,
                                   uint64_t,
                                   std::chrono::microseconds) {
    return PacingRate::Builder().setInterval(1000us).setBurstSize(10).build();
  });
  auto currentTime = Clock::now();
  pacer.refreshPacingRate(20, 100us); // These two values do not matter here
  pacer.onPacedWriteScheduled(currentTime);
  EXPECT_EQ(
      20 + conn.transportSettings.writeConnectionDataPacketsLimit,
      pacer.updateAndGetWriteBatchSize(currentTime + 1000us));

  // Query batch size again without calling onPacedWriteScheduled won't do timer
  // drift compensation. But token_ keeps the last compenstation.
  EXPECT_EQ(
      20 + conn.transportSettings.writeConnectionDataPacketsLimit,
      pacer.updateAndGetWriteBatchSize(currentTime + 2000us));

  // Consume a few:
  consumeTokensHelper(pacer, 3);

  EXPECT_EQ(
      20 + conn.transportSettings.writeConnectionDataPacketsLimit - 3,
      pacer.updateAndGetWriteBatchSize(currentTime + 2000us));
}

TEST_F(PacerTest, NextWriteTime) {
  EXPECT_EQ(0us, pacer.getTimeUntilNextWrite());

  pacer.setPacingRateCalculator([](const QuicConnectionStateBase&,
                                   uint64_t,
                                   uint64_t,
                                   std::chrono::microseconds rtt) {
    return PacingRate::Builder().setInterval(rtt).setBurstSize(10).build();
  });
  pacer.refreshPacingRate(20, 1000us);
  // Right after refresh, it's always 0us. You can always send right after an
  // ack.
  EXPECT_EQ(0us, pacer.getTimeUntilNextWrite());

  // Consume all the tokens:
  consumeTokensHelper(
      pacer, 10 + conn.transportSettings.writeConnectionDataPacketsLimit);

  // Then we use real delay:
  EXPECT_EQ(1000us, pacer.getTimeUntilNextWrite());
}

TEST_F(PacerTest, ImpossibleToPace) {
  conn.transportSettings.pacingTimerTickInterval = 1ms;
  pacer.setPacingRateCalculator([](const QuicConnectionStateBase& conn,
                                   uint64_t cwndBytes,
                                   uint64_t,
                                   std::chrono::microseconds rtt) {
    return PacingRate::Builder()
        .setInterval(rtt)
        .setBurstSize(cwndBytes / conn.udpSendPacketLen)
        .build();
  });
  pacer.refreshPacingRate(200 * conn.udpSendPacketLen, 100us);
  EXPECT_EQ(0us, pacer.getTimeUntilNextWrite());
  EXPECT_EQ(
      conn.transportSettings.writeConnectionDataPacketsLimit,
      pacer.updateAndGetWriteBatchSize(Clock::now()));
}

TEST_F(PacerTest, CachedBatchSize) {
  EXPECT_EQ(
      conn.transportSettings.writeConnectionDataPacketsLimit,
      pacer.getCachedWriteBatchSize());
  pacer.setPacingRateCalculator([](const QuicConnectionStateBase& conn,
                                   uint64_t cwndBytes,
                                   uint64_t,
                                   std::chrono::microseconds rtt) {
    return PacingRate::Builder()
        .setInterval(rtt)
        .setBurstSize(cwndBytes / conn.udpSendPacketLen * 2)
        .build();
  });
  pacer.refreshPacingRate(20 * conn.udpSendPacketLen, 100ms);
  EXPECT_EQ(40, pacer.getCachedWriteBatchSize());

  auto currentTime = Clock::now();
  pacer.onPacedWriteScheduled(currentTime);
  pacer.updateAndGetWriteBatchSize(currentTime);
  EXPECT_EQ(40, pacer.getCachedWriteBatchSize());

  pacer.onPacedWriteScheduled(currentTime + 100ms);
  pacer.updateAndGetWriteBatchSize(currentTime + 200ms);
  EXPECT_EQ(80, pacer.getCachedWriteBatchSize());
}

TEST_F(PacerTest, AppLimited) {
  conn.transportSettings.writeConnectionDataPacketsLimit = 12;
  pacer.setAppLimited(true);
  EXPECT_EQ(0us, pacer.getTimeUntilNextWrite());
  EXPECT_EQ(12, pacer.updateAndGetWriteBatchSize(Clock::now()));
}

TEST_F(PacerTest, Tokens) {
  // Pacer has tokens right after init:
  EXPECT_EQ(0us, pacer.getTimeUntilNextWrite());
  EXPECT_EQ(
      conn.transportSettings.writeConnectionDataPacketsLimit,
      pacer.updateAndGetWriteBatchSize(Clock::now()));

  // Consume all initial tokens:
  consumeTokensHelper(
      pacer, conn.transportSettings.writeConnectionDataPacketsLimit);

  // Pacing rate: 10 mss per 10 ms
  pacer.setPacingRateCalculator([](const QuicConnectionStateBase&,
                                   uint64_t,
                                   uint64_t,
                                   std::chrono::microseconds) {
    return PacingRate::Builder().setInterval(10ms).setBurstSize(10).build();
  });

  // These input doesn't matter, the rate calculator above returns fixed values.
  pacer.refreshPacingRate(100, 100ms);

  EXPECT_EQ(0us, pacer.getTimeUntilNextWrite());
  EXPECT_EQ(10, pacer.updateAndGetWriteBatchSize(Clock::now()));

  // Consume all tokens:
  consumeTokensHelper(pacer, 10);

  EXPECT_EQ(10ms, pacer.getTimeUntilNextWrite());
  EXPECT_EQ(0, pacer.updateAndGetWriteBatchSize(Clock::now()));

  // Do a schedule:
  auto curTime = Clock::now();
  pacer.onPacedWriteScheduled(curTime);
  // 10ms later you should have 10 mss credit:
  EXPECT_EQ(10, pacer.updateAndGetWriteBatchSize(curTime + 10ms));

  // Schedule again from this point:
  pacer.onPacedWriteScheduled(curTime + 10ms);
  // Then elapse another 10ms, and previous tokens hasn't been used:
  EXPECT_EQ(20, pacer.updateAndGetWriteBatchSize(curTime + 20ms));
}

} // namespace test
} // namespace quic
