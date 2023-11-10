/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#include <quic/codec/QuicPacketRebuilder.h>
#include <quic/codec/QuicWriteCodec.h>
#include <quic/flowcontrol/QuicFlowController.h>
#include <quic/state/QuicStreamFunctions.h>
#include <quic/state/SimpleFrameFunctions.h>

namespace quic {

PacketRebuilder::PacketRebuilder(
    RegularQuicPacketBuilder& regularBuilder,
    QuicConnectionStateBase& conn)
    : builder_(regularBuilder), conn_(conn) {}

uint64_t PacketRebuilder::getHeaderBytes() const {
  return builder_.getHeaderBytes();
}

PacketEvent PacketRebuilder::cloneOutstandingPacket(OutstandingPacket& packet) {
  // Either the packet has never been cloned before, or it's associatedEvent is
  // still in the outstandingPacketEvents set.
  DCHECK(
      !packet.associatedEvent ||
      conn_.outstandingPacketEvents.count(*packet.associatedEvent));
  if (!packet.associatedEvent) {
    auto packetNum = packet.packet.header.getPacketSequenceNum();
    DCHECK(!conn_.outstandingPacketEvents.count(packetNum));
    packet.associatedEvent = packetNum;
    conn_.outstandingPacketEvents.insert(packetNum);
    ++conn_.outstandingClonedPacketsCount;
  }
  return *packet.associatedEvent;
}

folly::Optional<PacketEvent> PacketRebuilder::rebuildFromPacket(
    OutstandingPacket& packet) {
  // TODO: if PMTU changes between the transmission of the original packet and
  // now, then we cannot clone everything in the packet.

  // TODO: make sure this cannot be called on handshake packets.
  bool writeSuccess = false;
  bool windowUpdateWritten = false;
  bool shouldWriteWindowUpdate = false;
  bool notPureAck = false;
  for (auto iter = packet.packet.frames.cbegin();
       iter != packet.packet.frames.cend();
       iter++) {
    const QuicWriteFrame& frame = *iter;
    switch (frame.type()) {
      case QuicWriteFrame::Type::WriteAckFrame_E: {
        const WriteAckFrame& ackFrame = *frame.asWriteAckFrame();
        auto& packetHeader = builder_.getPacketHeader();
        uint64_t ackDelayExponent =
            (packetHeader.getHeaderForm() == HeaderForm::Long)
            ? kDefaultAckDelayExponent
            : conn_.transportSettings.ackDelayExponent;
        AckBlocks ackBlocks;
        for (auto& block : ackFrame.ackBlocks) {
          ackBlocks.insert(block.start, block.end);
        }
        AckFrameMetaData meta(ackBlocks, ackFrame.ackDelay, ackDelayExponent);
        auto ackWriteResult = writeAckFrame(meta, builder_);
        writeSuccess = ackWriteResult.hasValue();
        break;
      }
      case QuicWriteFrame::Type::WriteStreamFrame_E: {
        const WriteStreamFrame& streamFrame = *frame.asWriteStreamFrame();
        auto stream = conn_.streamManager->getStream(streamFrame.streamId);
        if (stream && retransmittable(*stream)) {
          auto streamData = cloneRetransmissionBuffer(streamFrame, stream);
          auto bufferLen =
              streamData ? streamData->computeChainDataLength() : 0;
          auto dataLen = writeStreamFrameHeader(
              builder_,
              streamFrame.streamId,
              streamFrame.offset,
              bufferLen,
              bufferLen,
              streamFrame.fin);
          bool ret = dataLen.hasValue() && *dataLen == streamFrame.len;
          if (ret) {
            writeStreamFrameData(builder_, std::move(streamData), *dataLen);
            notPureAck = true;
            writeSuccess = true;
            break;
          }
          writeSuccess = false;
          break;
        }
        // If a stream is already Closed, we should not clone and resend this
        // stream data. But should we abort the cloning of this packet and
        // move on to the next packet? I'm gonna err on the aggressive side
        // for now and call it success.
        writeSuccess = true;
        break;
      }
      case QuicWriteFrame::Type::WriteCryptoFrame_E: {
        const WriteCryptoFrame& cryptoFrame = *frame.asWriteCryptoFrame();
        // initialStream and handshakeStream can only be in handshake packet,
        // so they are not clonable
        CHECK(!packet.isHandshake);
        // key update not supported
        DCHECK(
            packet.packet.header.getProtectionType() ==
            ProtectionType::KeyPhaseZero);
        auto& stream = conn_.cryptoState->oneRttStream;
        auto buf = cloneCryptoRetransmissionBuffer(cryptoFrame, stream);

        // No crypto data found to be cloned, just skip
        if (!buf) {
          writeSuccess = true;
          break;
        }
        auto cryptoWriteResult =
            writeCryptoFrame(cryptoFrame.offset, std::move(buf), builder_);
        bool ret = cryptoWriteResult.hasValue() &&
            cryptoWriteResult->offset == cryptoFrame.offset &&
            cryptoWriteResult->len == cryptoFrame.len;
        notPureAck |= ret;
        writeSuccess = ret;
        break;
      }
      case QuicWriteFrame::Type::MaxDataFrame_E: {
        shouldWriteWindowUpdate = true;
        auto ret = 0 != writeFrame(generateMaxDataFrame(conn_), builder_);
        windowUpdateWritten |= ret;
        notPureAck |= ret;
        writeSuccess = true;
        break;
      }
      case QuicWriteFrame::Type::MaxStreamDataFrame_E: {
        const MaxStreamDataFrame& maxStreamDataFrame =
            *frame.asMaxStreamDataFrame();
        auto stream =
            conn_.streamManager->getStream(maxStreamDataFrame.streamId);
        if (!stream || !stream->shouldSendFlowControl()) {
          writeSuccess = true;
          break;
        }
        shouldWriteWindowUpdate = true;
        auto ret =
            0 != writeFrame(generateMaxStreamDataFrame(*stream), builder_);
        windowUpdateWritten |= ret;
        notPureAck |= ret;
        writeSuccess = true;
        break;
      }
      case QuicWriteFrame::Type::PaddingFrame_E: {
        const PaddingFrame& paddingFrame = *frame.asPaddingFrame();
        writeSuccess = writeFrame(paddingFrame, builder_) != 0;
        break;
      }
      case QuicWriteFrame::Type::QuicSimpleFrame_E: {
        const QuicSimpleFrame& simpleFrame = *frame.asQuicSimpleFrame();
        auto updatedSimpleFrame =
            updateSimpleFrameOnPacketClone(conn_, simpleFrame);
        if (!updatedSimpleFrame) {
          writeSuccess = true;
          break;
        }
        bool ret =
            writeSimpleFrame(std::move(*updatedSimpleFrame), builder_) != 0;
        notPureAck |= ret;
        writeSuccess = ret;
        break;
      }
      default: {
        bool ret = writeFrame(QuicWriteFrame(frame), builder_) != 0;
        notPureAck |= ret;
        writeSuccess = ret;
        break;
      }
    }
    if (!writeSuccess) {
      return folly::none;
    }
  }
  // We shouldn't clone if:
  // (1) we only end up cloning acks and paddings.
  // (2) we should write window update, but didn't, and wrote nothing else.
  if (!notPureAck ||
      (shouldWriteWindowUpdate && !windowUpdateWritten && !writeSuccess)) {
    return folly::none;
  }
  return cloneOutstandingPacket(packet);
}

Buf PacketRebuilder::cloneCryptoRetransmissionBuffer(
    const WriteCryptoFrame& frame,
    const QuicCryptoStream& stream) {
  /**
   * Crypto's StreamBuffer is removed from retransmissionBuffer in 2 cases.
   * 1: Packet containing the buffer gets acked.
   * 2: Packet containing the buffer is marked loss.
   * They have to be covered by making sure we do not clone an already acked or
   * lost packet.
   */
  DCHECK(frame.len) << "WriteCryptoFrame cloning: frame is empty. " << conn_;
  auto iter = stream.retransmissionBuffer.find(frame.offset);

  // If the crypto stream is canceled somehow, just skip cloning this frame
  if (iter == stream.retransmissionBuffer.end()) {
    return nullptr;
  }
  DCHECK(iter->second.offset == frame.offset)
      << "WriteCryptoFrame cloning: offset mismatch. " << conn_;
  DCHECK(iter->second.data.chainLength() == frame.len)
      << "WriteCryptoFrame cloning: Len mismatch. " << conn_;
  return iter->second.data.front()->clone();
}

Buf PacketRebuilder::cloneRetransmissionBuffer(
    const WriteStreamFrame& frame,
    const QuicStreamState* stream) {
  /**
   * StreamBuffer is removed from retransmissionBuffer in 4 cases.
   * 1: After send or receive RST.
   * 2: Packet containing the buffer gets acked.
   * 3: Packet containing the buffer is marked loss.
   * 4: Skip (MIN_DATA or EXPIRED_DATA) frame is received with offset larger
   *    than what's in the retransmission buffer.
   *
   * Checking retransmittable() should cover first case. The latter three cases
   * have to be covered by making sure we do not clone an already acked, lost or
   * skipped packet.
   */
  DCHECK(stream);
  DCHECK(retransmittable(*stream));
  auto iter = stream->retransmissionBuffer.find(frame.offset);
  if (iter != stream->retransmissionBuffer.end()) {
    if (streamFrameMatchesRetransmitBuffer(*stream, frame, iter->second)) {
      DCHECK(!frame.len || !iter->second.data.empty())
          << "WriteStreamFrame cloning: frame is not empty but StreamBuffer has"
          << " empty data. " << conn_;
      return (frame.len ? iter->second.data.front()->clone() : nullptr);
    }
  }
  return nullptr;
}

} // namespace quic
