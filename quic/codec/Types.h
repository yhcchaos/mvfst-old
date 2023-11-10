/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#pragma once

#include <folly/Conv.h>
#include <folly/Optional.h>
#include <folly/io/Cursor.h>
#include <folly/small_vector.h>
#include <quic/QuicConstants.h>
#include <quic/QuicException.h>
#include <quic/codec/QuicConnectionId.h>
#include <quic/codec/QuicInteger.h>
#include <quic/common/BufUtil.h>
#include <quic/common/IntervalSet.h>
#include <quic/common/Variant.h>

/**
 * This details the types of objects that can be serialized or deserialized
 * over the wire.
 */

namespace quic {

using StreamId = uint64_t;
using PacketNum = uint64_t;

#if !FOLLY_MOBILE
template <class T, std::size_t N, class S>
using SmallVec = folly::small_vector<T, N, S>;
#else
template <class T, std::size_t N, class S>
using SmallVec = std::vector<T>;
#endif

enum class PacketNumberSpace : uint8_t {
  Initial,
  Handshake,
  AppData,
};

constexpr uint8_t kHeaderFormMask = 0x80;
constexpr auto kMaxPacketNumEncodingSize = 4;
constexpr auto kNumInitialAckBlocksPerFrame = 32;

template <class T>
using IntervalSetVec = SmallVec<T, kNumInitialAckBlocksPerFrame, uint16_t>;
using AckBlocks = IntervalSet<PacketNum, 1, IntervalSetVec>;

struct PaddingFrame {
  bool operator==(const PaddingFrame& /*rhs*/) const {
    return true;
  }
};

struct PingFrame {
  PingFrame() = default;

  bool operator==(const PingFrame& /*rhs*/) const {
    return true;
  }
};

/**
 * AckBlock represents a series of continuous packet sequences from
 * [startPacket, endPacket]
 */
struct AckBlock {
  PacketNum startPacket;
  PacketNum endPacket;

  AckBlock(PacketNum start, PacketNum end)
      : startPacket(start), endPacket(end) {}
};

/**
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                     Largest Acknowledged (i)                ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                          ACK Delay (i)                      ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                       ACK Block Count (i)                   ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                          ACK Blocks (*)                     ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                      First ACK Block (i)                    ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                             Gap (i)                         ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                    Additional ACK Block (i)                 ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct ReadAckFrame {
  PacketNum largestAcked;
  std::chrono::microseconds ackDelay{0us};
  // Should have at least 1 block.
  // These are ordered in descending order by start packet.
  using Vec = SmallVec<AckBlock, kNumInitialAckBlocksPerFrame, uint16_t>;
  Vec ackBlocks;

  bool operator==(const ReadAckFrame& /*rhs*/) const {
    // Can't compare ackBlocks, function is just here to appease compiler.
    return false;
  }
};

struct WriteAckFrame {
  // Since we don't need this to be an IntervalSet, they are stored directly
  // in a vector, in reverse order.
  // TODO should this be a small_vector?
  using AckBlockVec = std::vector<Interval<PacketNum>>;
  AckBlockVec ackBlocks;
  // Delay in sending ack from time that packet was received.
  std::chrono::microseconds ackDelay{0us};

  bool operator==(const WriteAckFrame& /*rhs*/) const {
    // Can't compare ackBlocks, function is just here to appease compiler.
    return false;
  }
};

struct RstStreamFrame {
  StreamId streamId;
  ApplicationErrorCode errorCode;
  uint64_t offset;

  RstStreamFrame(
      StreamId streamIdIn,
      ApplicationErrorCode errorCodeIn,
      uint64_t offsetIn)
      : streamId(streamIdIn), errorCode(errorCodeIn), offset(offsetIn) {}

  bool operator==(const RstStreamFrame& rhs) const {
    return streamId == rhs.streamId && errorCode == rhs.errorCode &&
        offset == rhs.offset;
  }
};

struct StopSendingFrame {
  StreamId streamId;
  ApplicationErrorCode errorCode;

  StopSendingFrame(StreamId streamIdIn, ApplicationErrorCode errorCodeIn)
      : streamId(streamIdIn), errorCode(errorCodeIn) {}

  bool operator==(const StopSendingFrame& rhs) const {
    return streamId == rhs.streamId && errorCode == rhs.errorCode;
  }
};

struct ReadCryptoFrame {
  uint64_t offset;
  Buf data;

  ReadCryptoFrame(uint64_t offsetIn, Buf dataIn)
      : offset(offsetIn), data(std::move(dataIn)) {}

  explicit ReadCryptoFrame(uint64_t offsetIn)
      : offset(offsetIn), data(folly::IOBuf::create(0)) {}

  // Stuff stored in a variant type needs to be copyable.
  // TODO: can we make this copyable only by the variant, but not
  // by anyone else.
  ReadCryptoFrame(const ReadCryptoFrame& other) {
    offset = other.offset;
    if (other.data) {
      data = other.data->clone();
    }
  }

  ReadCryptoFrame(ReadCryptoFrame&& other) noexcept {
    offset = other.offset;
    data = std::move(other.data);
  }

  ReadCryptoFrame& operator=(const ReadCryptoFrame& other) {
    offset = other.offset;
    if (other.data) {
      data = other.data->clone();
    }
    return *this;
  }

  ReadCryptoFrame& operator=(ReadCryptoFrame&& other) {
    offset = other.offset;
    data = std::move(other.data);
    return *this;
  }

  bool operator==(const ReadCryptoFrame& other) const {
    folly::IOBufEqualTo eq;
    return offset == other.offset && eq(data, other.data);
  }
};

struct WriteCryptoFrame {
  uint64_t offset;
  uint64_t len;

  WriteCryptoFrame(uint64_t offsetIn, uint64_t lenIn)
      : offset(offsetIn), len(lenIn) {}

  bool operator==(const WriteCryptoFrame& rhs) const {
    return offset == rhs.offset && len == rhs.len;
  }
};

struct ReadNewTokenFrame {
  Buf token;

  ReadNewTokenFrame(Buf tokenIn) : token(std::move(tokenIn)) {}

  // Stuff stored in a variant type needs to be copyable.
  // TODO: can we make this copyable only by the variant, but not
  // by anyone else.
  ReadNewTokenFrame(const ReadNewTokenFrame& other) {
    if (other.token) {
      token = other.token->clone();
    }
  }

  ReadNewTokenFrame& operator=(const ReadNewTokenFrame& other) {
    if (other.token) {
      token = other.token->clone();
    }
    return *this;
  }

  bool operator==(const ReadNewTokenFrame& other) const {
    folly::IOBufEqualTo eq;
    return eq(token, other.token);
  }
};

/**
 The structure of the stream frame used for writes.
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                         Stream ID (i)                       ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                         [Offset (i)]                        ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                         [Length (i)]                        ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                        Stream Data (*)                      ...
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
struct WriteStreamFrame {
  StreamId streamId;
  uint64_t offset;
  uint64_t len;
  bool fin;

  WriteStreamFrame(
      StreamId streamIdIn,
      uint64_t offsetIn,
      uint64_t lenIn,
      bool finIn)
      : streamId(streamIdIn), offset(offsetIn), len(lenIn), fin(finIn) {}

  bool operator==(const WriteStreamFrame& rhs) const {
    return streamId == rhs.streamId && offset == rhs.offset && len == rhs.len &&
        fin == rhs.fin;
  }
};

/**
 * The structure of the stream frame used for reads.
 */
struct ReadStreamFrame {
  StreamId streamId;
  uint64_t offset;
  Buf data;
  bool fin;

  ReadStreamFrame(
      StreamId streamIdIn,
      uint64_t offsetIn,
      Buf dataIn,
      bool finIn)
      : streamId(streamIdIn),
        offset(offsetIn),
        data(std::move(dataIn)),
        fin(finIn) {}

  ReadStreamFrame(StreamId streamIdIn, uint64_t offsetIn, bool finIn)
      : streamId(streamIdIn),
        offset(offsetIn),
        data(folly::IOBuf::create(0)),
        fin(finIn) {}

  // Stuff stored in a variant type needs to be copyable.
  // TODO: can we make this copyable only by the variant, but not
  // by anyone else.
  ReadStreamFrame(const ReadStreamFrame& other) {
    streamId = other.streamId;
    offset = other.offset;
    if (other.data) {
      data = other.data->clone();
    }
    fin = other.fin;
  }

  ReadStreamFrame(ReadStreamFrame&& other) noexcept {
    streamId = other.streamId;
    offset = other.offset;
    data = std::move(other.data);
    fin = other.fin;
  }

  ReadStreamFrame& operator=(const ReadStreamFrame& other) {
    streamId = other.streamId;
    offset = other.offset;
    if (other.data) {
      data = other.data->clone();
    }
    fin = other.fin;
    return *this;
  }

  ReadStreamFrame& operator=(ReadStreamFrame&& other) {
    streamId = other.streamId;
    offset = other.offset;
    data = std::move(other.data);
    fin = other.fin;
    return *this;
  }

  bool operator==(const ReadStreamFrame& other) const {
    folly::IOBufEqualTo eq;
    return streamId == other.streamId && offset == other.offset &&
        fin == other.fin && eq(data, other.data);
  }
};

struct MaxDataFrame {
  uint64_t maximumData;

  explicit MaxDataFrame(uint64_t maximumDataIn) : maximumData(maximumDataIn) {}

  bool operator==(const MaxDataFrame& rhs) const {
    return maximumData == rhs.maximumData;
  }
};

struct MaxStreamDataFrame {
  StreamId streamId;
  uint64_t maximumData;

  MaxStreamDataFrame(StreamId streamIdIn, uint64_t maximumDataIn)
      : streamId(streamIdIn), maximumData(maximumDataIn) {}

  bool operator==(const MaxStreamDataFrame& rhs) const {
    return streamId == rhs.streamId && maximumData == rhs.maximumData;
  }
};

// The MinStreamDataFrame is used by a receiver to inform
// a sender of the maximum amount of data that can be sent on a stream
// (like MAX_STREAM_DATA frame) and to request an update to the minimum
// retransmittable offset for this stream.
struct MinStreamDataFrame {
  StreamId streamId;
  uint64_t maximumData;
  uint64_t minimumStreamOffset;
  MinStreamDataFrame(
      StreamId streamIdIn,
      uint64_t maximumDataIn,
      uint64_t minimumStreamOffsetIn)
      : streamId(streamIdIn),
        maximumData(maximumDataIn),
        minimumStreamOffset(minimumStreamOffsetIn) {}

  bool operator==(const MinStreamDataFrame& rhs) const {
    return streamId == rhs.streamId && maximumData == rhs.maximumData &&
        minimumStreamOffset == rhs.minimumStreamOffset;
  }
};

// The ExpiredStreamDataFrame is used by a sender to
// inform a receiver of the minimum retransmittable offset for a stream.
struct ExpiredStreamDataFrame {
  StreamId streamId;
  uint64_t minimumStreamOffset;
  ExpiredStreamDataFrame(StreamId streamIdIn, uint64_t minimumStreamOffsetIn)
      : streamId(streamIdIn), minimumStreamOffset(minimumStreamOffsetIn) {}

  bool operator==(const ExpiredStreamDataFrame& rhs) const {
    return streamId == rhs.streamId &&
        minimumStreamOffset == rhs.minimumStreamOffset;
  }
};

struct MaxStreamsFrame {
  // A count of the cumulative number of streams
  uint64_t maxStreams;
  bool isForBidirectional{false};

  explicit MaxStreamsFrame(uint64_t maxStreamsIn, bool isBidirectionalIn)
      : maxStreams(maxStreamsIn), isForBidirectional(isBidirectionalIn) {}

  bool isForBidirectionalStream() const {
    return isForBidirectional;
  }

  bool isForUnidirectionalStream() {
    return !isForBidirectional;
  }

  bool operator==(const MaxStreamsFrame& rhs) const {
    return maxStreams == rhs.maxStreams &&
        isForBidirectional == rhs.isForBidirectional;
  }
};

struct DataBlockedFrame {
  // the connection-level limit at which blocking occurred
  uint64_t dataLimit;

  explicit DataBlockedFrame(uint64_t dataLimitIn) : dataLimit(dataLimitIn) {}

  bool operator==(const DataBlockedFrame& rhs) const {
    return dataLimit == rhs.dataLimit;
  }
};

struct StreamDataBlockedFrame {
  StreamId streamId;
  uint64_t dataLimit;

  StreamDataBlockedFrame(StreamId streamIdIn, uint64_t dataLimitIn)
      : streamId(streamIdIn), dataLimit(dataLimitIn) {}

  bool operator==(const StreamDataBlockedFrame& rhs) const {
    return streamId == rhs.streamId && dataLimit == rhs.dataLimit;
  }
};

struct StreamsBlockedFrame {
  uint64_t streamLimit;
  bool isForBidirectional{false};

  explicit StreamsBlockedFrame(uint64_t streamLimitIn, bool isBidirectionalIn)
      : streamLimit(streamLimitIn), isForBidirectional(isBidirectionalIn) {}

  bool isForBidirectionalStream() const {
    return isForBidirectional;
  }

  bool isForUnidirectionalStream() const {
    return !isForBidirectional;
  }

  bool operator==(const StreamsBlockedFrame& rhs) const {
    return streamLimit == rhs.streamLimit;
  }
};

struct NewConnectionIdFrame {
  uint64_t sequenceNumber;
  uint64_t retirePriorTo;
  ConnectionId connectionId;
  StatelessResetToken token;

  NewConnectionIdFrame(
      uint64_t sequenceNumberIn,
      uint64_t retirePriorToIn,
      ConnectionId connectionIdIn,
      StatelessResetToken tokenIn)
      : sequenceNumber(sequenceNumberIn),
        retirePriorTo(retirePriorToIn),
        connectionId(connectionIdIn),
        token(std::move(tokenIn)) {}

  bool operator==(const NewConnectionIdFrame& rhs) const {
    return sequenceNumber == rhs.sequenceNumber &&
        retirePriorTo == rhs.retirePriorTo &&
        connectionId == rhs.connectionId && token == rhs.token;
  }
};

struct RetireConnectionIdFrame {
  uint64_t sequenceNumber;
  explicit RetireConnectionIdFrame(uint64_t sequenceNumberIn)
      : sequenceNumber(sequenceNumberIn) {}

  bool operator==(const RetireConnectionIdFrame& rhs) const {
    return sequenceNumber == rhs.sequenceNumber;
  }
};

struct PathChallengeFrame {
  uint64_t pathData;

  explicit PathChallengeFrame(uint64_t pathDataIn) : pathData(pathDataIn) {}

  bool operator==(const PathChallengeFrame& rhs) const {
    return pathData == rhs.pathData;
  }

  bool operator!=(const PathChallengeFrame& rhs) const {
    return !(*this == rhs);
  }
};

struct PathResponseFrame {
  uint64_t pathData;

  explicit PathResponseFrame(uint64_t pathDataIn) : pathData(pathDataIn) {}

  bool operator==(const PathResponseFrame& rhs) const {
    return pathData == rhs.pathData;
  }
};

struct ConnectionCloseFrame {
  // Members are not const to allow this to be movable.
  QuicErrorCode errorCode;
  std::string reasonPhrase;
  // Per QUIC specification: type of frame that triggered the (close) error.
  // A value of 0 (PADDING frame) implies the frame type is unknown
  FrameType closingFrameType;

  ConnectionCloseFrame(
      QuicErrorCode errorCodeIn,
      std::string reasonPhraseIn,
      FrameType closingFrameTypeIn = FrameType::PADDING)
      : errorCode(std::move(errorCodeIn)),
        reasonPhrase(std::move(reasonPhraseIn)),
        closingFrameType(closingFrameTypeIn) {}

  FrameType getClosingFrameType() const noexcept {
    return closingFrameType;
  }

  bool operator==(const ConnectionCloseFrame& rhs) const {
    return errorCode == rhs.errorCode && reasonPhrase == rhs.reasonPhrase;
  }
};

// Frame to represent ones we skip
struct NoopFrame {
  bool operator==(const NoopFrame&) const {
    return true;
  }
};

struct StatelessReset {
  StatelessResetToken token;

  explicit StatelessReset(StatelessResetToken tokenIn)
      : token(std::move(tokenIn)) {}
};

#define QUIC_SIMPLE_FRAME(F, ...)         \
  F(StopSendingFrame, __VA_ARGS__)        \
  F(MinStreamDataFrame, __VA_ARGS__)      \
  F(ExpiredStreamDataFrame, __VA_ARGS__)  \
  F(PathChallengeFrame, __VA_ARGS__)      \
  F(PathResponseFrame, __VA_ARGS__)       \
  F(NewConnectionIdFrame, __VA_ARGS__)    \
  F(MaxStreamsFrame, __VA_ARGS__)         \
  F(RetireConnectionIdFrame, __VA_ARGS__) \
  F(PingFrame, __VA_ARGS__)

DECLARE_VARIANT_TYPE(QuicSimpleFrame, QUIC_SIMPLE_FRAME)

#define QUIC_FRAME(F, ...)               \
  F(PaddingFrame, __VA_ARGS__)           \
  F(RstStreamFrame, __VA_ARGS__)         \
  F(ConnectionCloseFrame, __VA_ARGS__)   \
  F(MaxDataFrame, __VA_ARGS__)           \
  F(MaxStreamDataFrame, __VA_ARGS__)     \
  F(DataBlockedFrame, __VA_ARGS__)       \
  F(StreamDataBlockedFrame, __VA_ARGS__) \
  F(StreamsBlockedFrame, __VA_ARGS__)    \
  F(ReadAckFrame, __VA_ARGS__)           \
  F(ReadStreamFrame, __VA_ARGS__)        \
  F(ReadCryptoFrame, __VA_ARGS__)        \
  F(ReadNewTokenFrame, __VA_ARGS__)      \
  F(QuicSimpleFrame, __VA_ARGS__)        \
  F(NoopFrame, __VA_ARGS__)

DECLARE_VARIANT_TYPE(QuicFrame, QUIC_FRAME)

#define QUIC_WRITE_FRAME(F, ...)         \
  F(PaddingFrame, __VA_ARGS__)           \
  F(RstStreamFrame, __VA_ARGS__)         \
  F(ConnectionCloseFrame, __VA_ARGS__)   \
  F(MaxDataFrame, __VA_ARGS__)           \
  F(MaxStreamDataFrame, __VA_ARGS__)     \
  F(DataBlockedFrame, __VA_ARGS__)       \
  F(StreamDataBlockedFrame, __VA_ARGS__) \
  F(StreamsBlockedFrame, __VA_ARGS__)    \
  F(WriteAckFrame, __VA_ARGS__)          \
  F(WriteStreamFrame, __VA_ARGS__)       \
  F(WriteCryptoFrame, __VA_ARGS__)       \
  F(QuicSimpleFrame, __VA_ARGS__)        \
  F(NoopFrame, __VA_ARGS__)

// Types of frames which are written.
DECLARE_VARIANT_TYPE(QuicWriteFrame, QUIC_WRITE_FRAME)

enum class HeaderForm : bool {
  Long = 1,
  Short = 0,
};

enum class ProtectionType {
  Initial,
  Handshake,
  ZeroRtt,
  KeyPhaseZero,
  KeyPhaseOne,
};

struct LongHeaderInvariant {
  QuicVersion version;
  ConnectionId srcConnId;
  ConnectionId dstConnId;

  LongHeaderInvariant(QuicVersion ver, ConnectionId scid, ConnectionId dcid);
};

// TODO: split this into read and write types.
struct LongHeader {
 public:
  virtual ~LongHeader() = default;

  static constexpr uint8_t kFixedBitMask = 0x40;
  static constexpr uint8_t kPacketTypeMask = 0x30;
  static constexpr uint8_t kReservedBitsMask = 0x0c;
  static constexpr uint8_t kPacketNumLenMask = 0x03;
  static constexpr uint8_t kTypeBitsMask = 0x0F;

  static constexpr uint8_t kTypeShift = 4;
  enum class Types : uint8_t {
    Initial = 0x0,
    ZeroRtt = 0x1,
    Handshake = 0x2,
    Retry = 0x3,
  };

  // Note this is defined in the header so it is inlined for performance.
  static PacketNumberSpace typeToPacketNumberSpace(Types longHeaderType) {
    switch (longHeaderType) {
      case LongHeader::Types::Initial:
      case LongHeader::Types::Retry:
        return PacketNumberSpace::Initial;
      case LongHeader::Types::Handshake:
        return PacketNumberSpace::Handshake;
      case LongHeader::Types::ZeroRtt:
        return PacketNumberSpace::AppData;
    }
    folly::assume_unreachable();
  }

  LongHeader(
      Types type,
      const ConnectionId& srcConnId,
      const ConnectionId& dstConnId,
      PacketNum packetNum,
      QuicVersion version,
      const std::string& token = std::string(),
      folly::Optional<ConnectionId> originalDstConnId = folly::none);

  LongHeader(
      Types type,
      LongHeaderInvariant invariant,
      const std::string& token = std::string(),
      folly::Optional<ConnectionId> originalDstConnId = folly::none);

  LongHeader(const LongHeader& other) = default;
  LongHeader(LongHeader&& other) = default;
  LongHeader& operator=(const LongHeader& other) = default;
  LongHeader& operator=(LongHeader&& other) = default;

  Types getHeaderType() const noexcept;
  const ConnectionId& getSourceConnId() const;
  const ConnectionId& getDestinationConnId() const;
  const folly::Optional<ConnectionId>& getOriginalDstConnId() const;
  QuicVersion getVersion() const;
  // Note this is defined in the header so it is inlined for performance.
  PacketNumberSpace getPacketNumberSpace() const {
    return typeToPacketNumberSpace(longHeaderType_);
  }
  ProtectionType getProtectionType() const;
  bool hasToken() const;
  const std::string& getToken() const;
  // Note this is defined in the header so it is inlined for performance.
  PacketNum getPacketSequenceNum() const {
    return packetSequenceNum_;
  }

  void setPacketNumber(PacketNum packetNum);

 private:
  PacketNum packetSequenceNum_{0};
  Types longHeaderType_;
  LongHeaderInvariant invariant_;
  std::string token_;
  folly::Optional<ConnectionId> originalDstConnId_;
};

struct ShortHeaderInvariant {
  ConnectionId destinationConnId;

  explicit ShortHeaderInvariant(ConnectionId dcid);
};

struct ShortHeader {
 public:
  virtual ~ShortHeader() = default;

  // There is also a spin bit which is 0x20 that we don't currently implement.
  static constexpr uint8_t kFixedBitMask = 0x40;
  static constexpr uint8_t kReservedBitsMask = 0x18;
  static constexpr uint8_t kKeyPhaseMask = 0x04;
  static constexpr uint8_t kPacketNumLenMask = 0x03;
  static constexpr uint8_t kTypeBitsMask = 0x1F;

  /**
   * The constructor for reading a packet.
   */
  ShortHeader(ProtectionType protectionType, ConnectionId connId);

  /**
   * The constructor for writing a packet.
   */
  ShortHeader(
      ProtectionType protectionType,
      ConnectionId connId,
      PacketNum packetNum);

  ProtectionType getProtectionType() const;
  PacketNumberSpace getPacketNumberSpace() const {
    return PacketNumberSpace::AppData;
  }
  PacketNum getPacketSequenceNum() const {
    return packetSequenceNum_;
  }
  const ConnectionId& getConnectionId() const;

  void setPacketNumber(PacketNum packetNum);

 private:
  ShortHeader() = delete;
  bool readInitialByte(uint8_t initalByte);
  bool readConnectionId(folly::io::Cursor& cursor);
  bool readPacketNum(
      PacketNum largestReceivedPacketNum,
      folly::io::Cursor& cursor);

 private:
  PacketNum packetSequenceNum_{0};
  ProtectionType protectionType_;
  ConnectionId connectionId_;
};

struct PacketHeader {
  ~PacketHeader();

  /* implicit */ PacketHeader(LongHeader&& longHeader);
  /* implicit */ PacketHeader(ShortHeader&& shortHeader);

  PacketHeader(PacketHeader&& other) noexcept;
  PacketHeader(const PacketHeader& other);

  PacketHeader& operator=(PacketHeader&& other) noexcept;
  PacketHeader& operator=(const PacketHeader& other);

  LongHeader* asLong();
  ShortHeader* asShort();

  const LongHeader* asLong() const;
  const ShortHeader* asShort() const;

  // Note this is defined in the header so it is inlined for performance.
  PacketNum getPacketSequenceNum() const {
    switch (headerForm_) {
      case HeaderForm::Long:
        return longHeader.getPacketSequenceNum();
      case HeaderForm::Short:
        return shortHeader.getPacketSequenceNum();
      default:
        folly::assume_unreachable();
    }
  }
  HeaderForm getHeaderForm() const;
  ProtectionType getProtectionType() const;
  // Note this is defined in the header so it is inlined for performance.
  PacketNumberSpace getPacketNumberSpace() const {
    switch (headerForm_) {
      case HeaderForm::Long:
        return longHeader.getPacketNumberSpace();
      case HeaderForm::Short:
        return shortHeader.getPacketNumberSpace();
      default:
        folly::assume_unreachable();
    }
  }

 private:
  void destroyHeader();

  union {
    LongHeader longHeader;
    ShortHeader shortHeader;
  };

  HeaderForm headerForm_;
};

ProtectionType longHeaderTypeToProtectionType(LongHeader::Types type);

struct StreamTypeField {
 public:
  explicit StreamTypeField(uint8_t field) : field_(field) {}
  bool hasFin() const;
  bool hasDataLength() const;
  bool hasOffset() const;
  uint8_t fieldValue() const;

  struct Builder {
   public:
    Builder() : field_(static_cast<uint8_t>(FrameType::STREAM)) {}
    Builder& setFin();
    Builder& setOffset();
    Builder& setLength();

    StreamTypeField build();

   private:
    uint8_t field_;
  };

 private:
  // Stream Frame specific:
  static constexpr uint8_t kFinBit = 0x01;
  static constexpr uint8_t kDataLengthBit = 0x02;
  static constexpr uint8_t kOffsetBit = 0x04;

  uint8_t field_;
};

struct VersionNegotiationPacket {
  uint8_t packetType;
  ConnectionId sourceConnectionId;
  ConnectionId destinationConnectionId;
  std::vector<QuicVersion> versions;

  VersionNegotiationPacket(
      uint8_t packetTypeIn,
      ConnectionId sourceConnectionIdIn,
      ConnectionId destinationConnectionIdIn)
      : packetType(packetTypeIn),
        sourceConnectionId(sourceConnectionIdIn),
        destinationConnectionId(destinationConnectionIdIn) {}
};

/**
 * Common struct for regular read and write packets.
 */
struct RegularPacket {
  PacketHeader header;

  explicit RegularPacket(PacketHeader&& headerIn)
      : header(std::move(headerIn)) {}
};

/**
 * A representation of a regular packet that is read from the network.
 * This could be either Cleartext or Encrypted packets in long or short form.
 * Cleartext packets include Client Initial, Client Cleartext, Non-Final Server
 * Cleartext packet or Final Server Cleartext packet. Encrypted packets
 * include 0-RTT, 1-RTT Phase 0 and 1-RTT Phase 1 packets.
 */
struct RegularQuicPacket : public RegularPacket {
  using Vec = SmallVec<QuicFrame, 4, uint16_t>;
  Vec frames;

  explicit RegularQuicPacket(PacketHeader&& headerIn)
      : RegularPacket(std::move(headerIn)) {}
};

/**
 * A representation of a regular packet that is written to the network.
 */
struct RegularQuicWritePacket : public RegularPacket {
  using Vec = SmallVec<QuicWriteFrame, 4, uint16_t>;
  Vec frames;

  explicit RegularQuicWritePacket(PacketHeader&& headerIn)
      : RegularPacket(std::move(headerIn)) {}
};

/**
 * Returns whether the header is long or short from the initial byte of
 * the QUIC packet.
 *
 * This function is version invariant.
 */
HeaderForm getHeaderForm(uint8_t headerValue);

std::string toString(LongHeader::Types type);

std::string toString(QuicErrorCode code);

inline std::ostream& operator<<(
    std::ostream& os,
    const LongHeader::Types& type) {
  os << toString(type);
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const PacketHeader& header) {
  auto shortHeader = header.asShort();
  if (shortHeader) {
    os << "header=short"
       << " protectionType=" << (int)shortHeader->getProtectionType();
  } else {
    auto longHeader = header.asLong();
    os << "header=long"
       << " protectionType=" << (int)longHeader->getProtectionType()
       << " type=" << std::hex << (int)longHeader->getHeaderType();
  }
  return os;
}

std::string toString(PacketNumberSpace pnSpace);

std::string toString(FrameType frame);

std::string toString(QuicVersion version);

inline std::ostream& operator<<(std::ostream& os, PacketNumberSpace pnSpace) {
  return os << toString(pnSpace);
}

std::string toString(ProtectionType protectionType);

} // namespace quic
