// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved

#include <quic/state/PendingPathRateLimiter.h>
#include <glog/logging.h>

namespace quic {

void PendingPathRateLimiter::onPacketSent(uint64_t sentBytes) {
  DCHECK_GE(credit_, sentBytes);
  credit_ -= sentBytes;
}

uint64_t PendingPathRateLimiter::currentCredit(
    TimePoint checkTime,
    std::chrono::microseconds rtt) noexcept {
  if ((!lastChecked_.hasValue()) || (checkTime > *lastChecked_ + rtt)) {
    lastChecked_ = checkTime;
    credit_ = maxCredit_;
  }
  return credit_;
}
} // namespace quic
