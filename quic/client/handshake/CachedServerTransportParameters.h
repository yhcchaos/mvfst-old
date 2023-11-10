/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#pragma once

#include <quic/QuicConstants.h>

#include <cstdint>

namespace quic {

struct CachedServerTransportParameters {
  QuicVersion negotiatedVersion;
  uint64_t idleTimeout;
  uint64_t maxRecvPacketSize;
  uint64_t initialMaxData;
  uint64_t initialMaxStreamDataBidiLocal;
  uint64_t initialMaxStreamDataBidiRemote;
  uint64_t initialMaxStreamDataUni;
  uint64_t initialMaxStreamsBidi;
  uint64_t initialMaxStreamsUni;
};

} // namespace quic
