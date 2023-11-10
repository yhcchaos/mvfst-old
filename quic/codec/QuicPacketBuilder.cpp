/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#include <quic/codec/QuicPacketBuilder.h>

#include <folly/Random.h>
#include <quic/codec/PacketNumber.h>

namespace quic {

PacketNumEncodingResult encodeLongHeaderHelper(
    const LongHeader& longHeader,
    BufAppender& appender,
    uint32_t& spaceCounter,
    PacketNum largestAckedPacketNum);

PacketNumEncodingResult encodeLongHeaderHelper(
    const LongHeader& longHeader,
    BufAppender& appender,
    uint32_t& spaceCounter,
    PacketNum largestAckedPacketNum) {
  uint8_t initialByte = kHeaderFormMask | LongHeader::kFixedBitMask |
      (static_cast<uint8_t>(longHeader.getHeaderType())
       << LongHeader::kTypeShift);
  PacketNumEncodingResult encodedPacketNum = encodePacketNumber(
      longHeader.getPacketSequenceNum(), largestAckedPacketNum);
  initialByte &= ~LongHeader::kReservedBitsMask;
  initialByte |= (encodedPacketNum.length - 1);

  if (longHeader.getHeaderType() == LongHeader::Types::Retry) {
    initialByte &= 0xF0;
    auto odcidSize = longHeader.getOriginalDstConnId()->size();
    initialByte |= (odcidSize == 0 ? 0 : odcidSize - 3);
  }

  appender.writeBE<uint8_t>(initialByte);
  bool isInitial = longHeader.getHeaderType() == LongHeader::Types::Initial;
  uint64_t tokenHeaderLength = 0;
  const std::string& token = longHeader.getToken();
  if (isInitial) {
    uint64_t tokenLength = token.size();
    QuicInteger tokenLengthInt(tokenLength);
    tokenHeaderLength = tokenLengthInt.getSize() + tokenLength;
  }
  auto version = longHeader.getVersion();
  if (version == QuicVersion::MVFST_OLD) {
    auto longHeaderSize = sizeof(uint8_t) /* initialByte */ +
        sizeof(QuicVersionType) + sizeof(uint8_t) /* DCIL | SCIL */ +
        longHeader.getSourceConnId().size() +
        longHeader.getDestinationConnId().size() + tokenHeaderLength +
        kMaxPacketLenSize + encodedPacketNum.length;
    if (spaceCounter < longHeaderSize) {
      spaceCounter = 0;
    } else {
      spaceCounter -= longHeaderSize;
    }
    appender.writeBE<uint32_t>(folly::to<uint32_t>(longHeader.getVersion()));
    auto connidSize = encodeConnectionIdLengths(
        longHeader.getDestinationConnId().size(),
        longHeader.getSourceConnId().size());
    appender.writeBE<uint8_t>(connidSize);
    appender.push(
        longHeader.getDestinationConnId().data(),
        longHeader.getDestinationConnId().size());
    appender.push(
        longHeader.getSourceConnId().data(),
        longHeader.getSourceConnId().size());
  } else {
    auto longHeaderSize = sizeof(uint8_t) /* initialByte */ +
        sizeof(QuicVersionType) + sizeof(uint8_t) +
        longHeader.getSourceConnId().size() + sizeof(uint8_t) +
        longHeader.getDestinationConnId().size() + tokenHeaderLength +
        kMaxPacketLenSize + encodedPacketNum.length;
    if (spaceCounter < longHeaderSize) {
      spaceCounter = 0;
    } else {
      spaceCounter -= longHeaderSize;
    }
    appender.writeBE<uint32_t>(folly::to<uint32_t>(longHeader.getVersion()));
    appender.writeBE<uint8_t>(longHeader.getDestinationConnId().size());
    appender.push(
        longHeader.getDestinationConnId().data(),
        longHeader.getDestinationConnId().size());
    appender.writeBE<uint8_t>(longHeader.getSourceConnId().size());
    appender.push(
        longHeader.getSourceConnId().data(),
        longHeader.getSourceConnId().size());
  }

  if (isInitial) {
    uint64_t tokenLength = token.size();
    QuicInteger tokenLengthInt(tokenLength);
    tokenLengthInt.encode(appender);
    if (tokenLength > 0) {
      appender.push((const uint8_t*)token.data(), token.size());
    }
  }

  if (longHeader.getHeaderType() == LongHeader::Types::Retry) {
    auto& originalDstConnId = longHeader.getOriginalDstConnId();
    appender.writeBE<uint8_t>(longHeader.getOriginalDstConnId()->size());
    appender.push(originalDstConnId->data(), originalDstConnId->size());

    // Write the retry token
    CHECK(!token.empty()) << "Retry packet must contain a token";
    appender.push((const uint8_t*)token.data(), token.size());
  }
  // defer write of the packet num and length till payload has been computed
  return encodedPacketNum;
}

RegularQuicPacketBuilder::RegularQuicPacketBuilder(
    uint32_t remainingBytes,
    PacketHeader header,
    PacketNum largestAckedPacketNum,
    QuicVersion version)
    : remainingBytes_(remainingBytes),
      packet_(std::move(header)),
      header_(folly::IOBuf::create(kLongHeaderHeaderSize)),
      body_(folly::IOBuf::create(kAppenderGrowthSize)),
      headerAppender_(header_.get(), kLongHeaderHeaderSize),
      bodyAppender_(body_.get(), kAppenderGrowthSize),
      version_(version) {
  writeHeaderBytes(largestAckedPacketNum);
}

uint32_t RegularQuicPacketBuilder::getHeaderBytes() const {
  bool isLongHeader = packet_.header.getHeaderForm() == HeaderForm::Long;
  CHECK(packetNumberEncoding_)
      << "packetNumberEncoding_ should be valid after ctor";
  return folly::to<uint32_t>(header_->computeChainDataLength()) +
      (isLongHeader ? packetNumberEncoding_->length + kMaxPacketLenSize : 0);
}

uint32_t RegularQuicPacketBuilder::remainingSpaceInPkt() const {
  return remainingBytes_;
}

void RegularQuicPacketBuilder::writeBE(uint8_t data) {
  bodyAppender_.writeBE<uint8_t>(data);
  remainingBytes_ -= sizeof(data);
}

void RegularQuicPacketBuilder::writeBE(uint16_t data) {
  bodyAppender_.writeBE<uint16_t>(data);
  remainingBytes_ -= sizeof(data);
}

void RegularQuicPacketBuilder::writeBE(uint64_t data) {
  bodyAppender_.writeBE<uint64_t>(data);
  remainingBytes_ -= sizeof(data);
}

void RegularQuicPacketBuilder::write(const QuicInteger& quicInteger) {
  remainingBytes_ -= quicInteger.encode(bodyAppender_);
}

void RegularQuicPacketBuilder::appendBytes(
    PacketNum value,
    uint8_t byteNumber) {
  appendBytes(bodyAppender_, value, byteNumber);
}

void RegularQuicPacketBuilder::appendBytes(
    BufAppender& appender,
    PacketNum value,
    uint8_t byteNumber) {
  auto bigValue = folly::Endian::big(value);
  appender.push(
      (uint8_t*)&bigValue + sizeof(bigValue) - byteNumber, byteNumber);
  remainingBytes_ -= byteNumber;
}

void RegularQuicPacketBuilder::insert(std::unique_ptr<folly::IOBuf> buf) {
  remainingBytes_ -= buf->computeChainDataLength();
  bodyAppender_.insert(std::move(buf));
}

void RegularQuicPacketBuilder::appendFrame(QuicWriteFrame frame) {
  packet_.frames.push_back(std::move(frame));
}

RegularQuicPacketBuilder::Packet RegularQuicPacketBuilder::buildPacket() && {
  // at this point everything should been set in the packet_
  LongHeader* longHeader = packet_.header.asLong();
  size_t minBodySize = kMaxPacketNumEncodingSize -
      packetNumberEncoding_->length + sizeof(Sample);
  size_t extraDataWritten = 0;
  size_t bodyLength = body_->computeChainDataLength();
  while (bodyLength + extraDataWritten + cipherOverhead_ < minBodySize &&
         !packet_.frames.empty() && remainingBytes_ > kMaxPacketLenSize) {
    // We can add padding frames, but we don't need to store them.
    QuicInteger paddingType(static_cast<uint8_t>(FrameType::PADDING));
    write(paddingType);
    extraDataWritten++;
  }
  if (longHeader && longHeader->getHeaderType() != LongHeader::Types::Retry) {
    QuicInteger pktLen(
        packetNumberEncoding_->length + body_->computeChainDataLength() +
        cipherOverhead_);
    pktLen.encode(headerAppender_);
    appendBytes(
        headerAppender_,
        packetNumberEncoding_->result,
        packetNumberEncoding_->length);
  }
  return Packet(std::move(packet_), std::move(header_), std::move(body_));
}

void RegularQuicPacketBuilder::writeHeaderBytes(
    PacketNum largestAckedPacketNum) {
  if (packet_.header.getHeaderForm() == HeaderForm::Long) {
    LongHeader& longHeader = *packet_.header.asLong();
    encodeLongHeader(longHeader, largestAckedPacketNum);
  } else {
    ShortHeader& shortHeader = *packet_.header.asShort();
    encodeShortHeader(shortHeader, largestAckedPacketNum);
  }
}

void RegularQuicPacketBuilder::encodeLongHeader(
    const LongHeader& longHeader,
    PacketNum largestAckedPacketNum) {
  packetNumberEncoding_ = encodeLongHeaderHelper(
      longHeader, headerAppender_, remainingBytes_, largestAckedPacketNum);
}

void RegularQuicPacketBuilder::encodeShortHeader(
    const ShortHeader& shortHeader,
    PacketNum largestAckedPacketNum) {
  packetNumberEncoding_ = encodePacketNumber(
      shortHeader.getPacketSequenceNum(), largestAckedPacketNum);
  if (remainingBytes_ < 1U + packetNumberEncoding_->length +
          shortHeader.getConnectionId().size()) {
    remainingBytes_ = 0;
    return;
  }
  uint8_t initialByte =
      ShortHeader::kFixedBitMask | (packetNumberEncoding_->length - 1);
  initialByte &= ~ShortHeader::kReservedBitsMask;
  if (shortHeader.getProtectionType() == ProtectionType::KeyPhaseOne) {
    initialByte |= ShortHeader::kKeyPhaseMask;
  }
  headerAppender_.writeBE<uint8_t>(initialByte);
  --remainingBytes_;

  headerAppender_.push(
      shortHeader.getConnectionId().data(),
      shortHeader.getConnectionId().size());
  remainingBytes_ -= shortHeader.getConnectionId().size();
  appendBytes(
      headerAppender_,
      packetNumberEncoding_->result,
      packetNumberEncoding_->length);
}

void RegularQuicPacketBuilder::push(const uint8_t* data, size_t len) {
  bodyAppender_.push(data, len);
  remainingBytes_ -= len;
}

bool RegularQuicPacketBuilder::canBuildPacket() const noexcept {
  return remainingBytes_ != 0;
}

const PacketHeader& RegularQuicPacketBuilder::getPacketHeader() const {
  return packet_.header;
}

void RegularQuicPacketBuilder::setCipherOverhead(uint8_t overhead) noexcept {
  cipherOverhead_ = overhead;
}

QuicVersion RegularQuicPacketBuilder::getVersion() const {
  return version_;
}

StatelessResetPacketBuilder::StatelessResetPacketBuilder(
    uint16_t maxPacketSize,
    const StatelessResetToken& resetToken)
    : data_(folly::IOBuf::create(kAppenderGrowthSize)) {
  BufAppender appender(data_.get(), kAppenderGrowthSize);
  // TODO: randomize the length
  uint16_t randomOctetLength = maxPacketSize - resetToken.size() - 1;
  uint8_t initialByte = ShortHeader::kFixedBitMask;
  appender.writeBE<uint8_t>(initialByte);
  auto randomOctets = folly::IOBuf::create(randomOctetLength);
  folly::Random::secureRandom(randomOctets->writableData(), randomOctetLength);
  appender.push(randomOctets->data(), randomOctetLength);
  appender.push(resetToken.data(), resetToken.size());
}

Buf StatelessResetPacketBuilder::buildPacket() && {
  return std::move(data_);
}

VersionNegotiationPacketBuilder::VersionNegotiationPacketBuilder(
    ConnectionId sourceConnectionId,
    ConnectionId destinationConnectionId,
    const std::vector<QuicVersion>& versions)
    : remainingBytes_(kDefaultUDPSendPacketLen),
      packet_(
          generateRandomPacketType(),
          sourceConnectionId,
          destinationConnectionId),
      data_(folly::IOBuf::create(kAppenderGrowthSize)) {
  writeVersionNegotiationPacket(versions);
}

uint32_t VersionNegotiationPacketBuilder::remainingSpaceInPkt() {
  return remainingBytes_;
}

std::pair<VersionNegotiationPacket, Buf>
VersionNegotiationPacketBuilder::buildPacket() && {
  return std::make_pair<VersionNegotiationPacket, Buf>(
      std::move(packet_), std::move(data_));
}

void VersionNegotiationPacketBuilder::writeVersionNegotiationPacket(
    const std::vector<QuicVersion>& versions) {
  // Write header
  BufAppender appender(data_.get(), kAppenderGrowthSize);
  appender.writeBE<decltype(packet_.packetType)>(packet_.packetType);
  remainingBytes_ -= sizeof(decltype(packet_.packetType));
  appender.writeBE(
      static_cast<QuicVersionType>(QuicVersion::VERSION_NEGOTIATION));
  remainingBytes_ -= sizeof(QuicVersionType);
  appender.writeBE<uint8_t>(packet_.destinationConnectionId.size());
  remainingBytes_ -= sizeof(uint8_t);
  appender.push(
      packet_.destinationConnectionId.data(),
      packet_.destinationConnectionId.size());
  remainingBytes_ -= packet_.destinationConnectionId.size();
  appender.writeBE<uint8_t>(packet_.sourceConnectionId.size());
  remainingBytes_ -= sizeof(uint8_t);
  appender.push(
      packet_.sourceConnectionId.data(), packet_.sourceConnectionId.size());
  remainingBytes_ -= packet_.sourceConnectionId.size();
  // Write versions
  for (auto version : versions) {
    if (remainingBytes_ < sizeof(QuicVersionType)) {
      break;
    }
    appender.writeBE<QuicVersionType>(static_cast<QuicVersionType>(version));
    remainingBytes_ -= sizeof(QuicVersionType);
    packet_.versions.push_back(version);
  }
}

uint8_t VersionNegotiationPacketBuilder::generateRandomPacketType() const {
  // TODO: change this back to generating random packet type after we rollout
  // draft-13. For now the 0 packet type will make sure that the version
  // negotiation packet is not interpreted as a long header.
  // folly::Random::secureRandom<decltype(packet_.packetType)>();
  return kHeaderFormMask;
}

bool VersionNegotiationPacketBuilder::canBuildPacket() const noexcept {
  return remainingBytes_ != 0;
}
} // namespace quic
