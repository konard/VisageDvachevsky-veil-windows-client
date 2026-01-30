#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "transport/mux/mux_codec.h"

namespace veil::tests {

TEST(MuxCodecTests, DataFrameRoundTrip) {
  std::vector<std::uint8_t> payload{0x01, 0x02, 0x03, 0x04};
  auto frame = mux::make_data_frame(42, 100, false, payload);
  auto encoded = mux::MuxCodec::encode(frame);
  auto decoded = mux::MuxCodec::decode(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->kind, mux::FrameKind::kData);
  EXPECT_EQ(decoded->data.stream_id, 42U);
  EXPECT_EQ(decoded->data.sequence, 100U);
  EXPECT_FALSE(decoded->data.fin);
  EXPECT_EQ(decoded->data.payload, payload);
}

TEST(MuxCodecTests, DataFrameWithFin) {
  std::vector<std::uint8_t> payload{0xAA, 0xBB};
  auto frame = mux::make_data_frame(1, 50, true, payload);
  auto encoded = mux::MuxCodec::encode(frame);
  auto decoded = mux::MuxCodec::decode(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->data.fin);
}

TEST(MuxCodecTests, AckFrameRoundTrip) {
  auto frame = mux::make_ack_frame(7, 200, 0xDEADBEEF);
  auto encoded = mux::MuxCodec::encode(frame);
  auto decoded = mux::MuxCodec::decode(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->kind, mux::FrameKind::kAck);
  EXPECT_EQ(decoded->ack.stream_id, 7U);
  EXPECT_EQ(decoded->ack.ack, 200U);
  EXPECT_EQ(decoded->ack.bitmap, 0xDEADBEEFU);
}

TEST(MuxCodecTests, ControlFrameRoundTrip) {
  std::vector<std::uint8_t> payload{0x10, 0x20, 0x30};
  auto frame = mux::make_control_frame(0x05, payload);
  auto encoded = mux::MuxCodec::encode(frame);
  auto decoded = mux::MuxCodec::decode(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->kind, mux::FrameKind::kControl);
  EXPECT_EQ(decoded->control.type, 0x05);
  EXPECT_EQ(decoded->control.payload, payload);
}

TEST(MuxCodecTests, RejectsEmptyData) {
  auto decoded = mux::MuxCodec::decode({});
  EXPECT_FALSE(decoded.has_value());
}

TEST(MuxCodecTests, RejectsTruncatedDataFrame) {
  std::vector<std::uint8_t> payload{0x01, 0x02};
  auto frame = mux::make_data_frame(1, 1, false, payload);
  auto encoded = mux::MuxCodec::encode(frame);
  encoded.pop_back();  // Remove one byte to make it truncated
  auto decoded = mux::MuxCodec::decode(encoded);
  EXPECT_FALSE(decoded.has_value());
}

TEST(MuxCodecTests, RejectsTruncatedAckFrame) {
  auto frame = mux::make_ack_frame(1, 1, 0);
  auto encoded = mux::MuxCodec::encode(frame);
  encoded.pop_back();
  auto decoded = mux::MuxCodec::decode(encoded);
  EXPECT_FALSE(decoded.has_value());
}

TEST(MuxCodecTests, RejectsUnknownFrameKind) {
  std::vector<std::uint8_t> data{0xFF, 0x00, 0x00};  // Unknown kind
  auto decoded = mux::MuxCodec::decode(data);
  EXPECT_FALSE(decoded.has_value());
}

TEST(MuxCodecTests, EncodedSizeIsAccurate) {
  std::vector<std::uint8_t> payload{1, 2, 3, 4, 5};
  auto data_frame = mux::make_data_frame(1, 1, true, payload);
  EXPECT_EQ(mux::MuxCodec::encoded_size(data_frame), mux::MuxCodec::encode(data_frame).size());

  auto ack_frame = mux::make_ack_frame(2, 3, 0x12345678);
  EXPECT_EQ(mux::MuxCodec::encoded_size(ack_frame), mux::MuxCodec::encode(ack_frame).size());

  auto ctrl_frame = mux::make_control_frame(0x01, payload);
  EXPECT_EQ(mux::MuxCodec::encoded_size(ctrl_frame), mux::MuxCodec::encode(ctrl_frame).size());
}

TEST(MuxCodecTests, EmptyDataFramePayload) {
  auto frame = mux::make_data_frame(123, 456, false, {});
  auto encoded = mux::MuxCodec::encode(frame);
  auto decoded = mux::MuxCodec::decode(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->data.payload.empty());
}

TEST(MuxCodecTests, EmptyControlFramePayload) {
  auto frame = mux::make_control_frame(0x00, {});
  auto encoded = mux::MuxCodec::encode(frame);
  auto decoded = mux::MuxCodec::decode(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_TRUE(decoded->control.payload.empty());
}

TEST(MuxCodecTests, LargeStreamIdAndSequence) {
  auto frame = mux::make_data_frame(0xFFFFFFFFFFFFFFFF, 0x123456789ABCDEF0, false, {0x42});
  auto encoded = mux::MuxCodec::encode(frame);
  auto decoded = mux::MuxCodec::decode(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->data.stream_id, 0xFFFFFFFFFFFFFFFFULL);
  EXPECT_EQ(decoded->data.sequence, 0x123456789ABCDEF0ULL);
}

// PERFORMANCE (Issue #97): Zero-copy codec tests

TEST(MuxCodecTests, EncodeToDataFrame) {
  std::vector<std::uint8_t> payload{0x01, 0x02, 0x03, 0x04};
  auto frame = mux::make_data_frame(42, 100, false, payload);

  std::vector<std::uint8_t> buffer(mux::MuxCodec::encoded_size(frame));
  auto size = mux::MuxCodec::encode_to(frame, buffer);

  EXPECT_EQ(size, buffer.size());

  // Verify encoding matches the original encode function
  auto original = mux::MuxCodec::encode(frame);
  EXPECT_EQ(buffer, original);
}

TEST(MuxCodecTests, EncodeToAckFrame) {
  auto frame = mux::make_ack_frame(7, 200, 0xDEADBEEF);

  std::vector<std::uint8_t> buffer(mux::MuxCodec::encoded_size(frame));
  auto size = mux::MuxCodec::encode_to(frame, buffer);

  EXPECT_EQ(size, buffer.size());

  // Verify encoding matches
  auto original = mux::MuxCodec::encode(frame);
  EXPECT_EQ(buffer, original);
}

TEST(MuxCodecTests, EncodeToControlFrame) {
  std::vector<std::uint8_t> payload{0x10, 0x20, 0x30};
  auto frame = mux::make_control_frame(0x05, payload);

  std::vector<std::uint8_t> buffer(mux::MuxCodec::encoded_size(frame));
  auto size = mux::MuxCodec::encode_to(frame, buffer);

  EXPECT_EQ(size, buffer.size());

  auto original = mux::MuxCodec::encode(frame);
  EXPECT_EQ(buffer, original);
}

TEST(MuxCodecTests, EncodeToHeartbeatFrame) {
  std::vector<std::uint8_t> payload{0xAA, 0xBB};
  auto frame = mux::make_heartbeat_frame(123456789, 42, payload);

  std::vector<std::uint8_t> buffer(mux::MuxCodec::encoded_size(frame));
  auto size = mux::MuxCodec::encode_to(frame, buffer);

  EXPECT_EQ(size, buffer.size());

  auto original = mux::MuxCodec::encode(frame);
  EXPECT_EQ(buffer, original);
}

TEST(MuxCodecTests, EncodeToBufferTooSmall) {
  std::vector<std::uint8_t> payload{0x01, 0x02, 0x03, 0x04};
  auto frame = mux::make_data_frame(42, 100, false, payload);

  std::vector<std::uint8_t> buffer(5);  // Too small
  auto size = mux::MuxCodec::encode_to(frame, buffer);

  EXPECT_EQ(size, 0u);  // Should return 0 on failure
}

TEST(MuxCodecTests, DecodeViewDataFrame) {
  std::vector<std::uint8_t> payload{0x01, 0x02, 0x03, 0x04};
  auto frame = mux::make_data_frame(42, 100, true, payload);
  auto encoded = mux::MuxCodec::encode(frame);

  auto view = mux::MuxCodec::decode_view(encoded);

  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->kind, mux::FrameKind::kData);
  EXPECT_EQ(view->data.stream_id, 42U);
  EXPECT_EQ(view->data.sequence, 100U);
  EXPECT_TRUE(view->data.fin);

  // The payload should be a view into the encoded buffer
  EXPECT_EQ(view->data.payload.size(), payload.size());
  EXPECT_EQ(std::vector<std::uint8_t>(view->data.payload.begin(), view->data.payload.end()), payload);
}

TEST(MuxCodecTests, DecodeViewAckFrame) {
  auto frame = mux::make_ack_frame(7, 200, 0xDEADBEEF);
  auto encoded = mux::MuxCodec::encode(frame);

  auto view = mux::MuxCodec::decode_view(encoded);

  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->kind, mux::FrameKind::kAck);
  EXPECT_EQ(view->ack.stream_id, 7U);
  EXPECT_EQ(view->ack.ack, 200U);
  EXPECT_EQ(view->ack.bitmap, 0xDEADBEEFU);
}

TEST(MuxCodecTests, DecodeViewControlFrame) {
  std::vector<std::uint8_t> payload{0x10, 0x20, 0x30};
  auto frame = mux::make_control_frame(0x05, payload);
  auto encoded = mux::MuxCodec::encode(frame);

  auto view = mux::MuxCodec::decode_view(encoded);

  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->kind, mux::FrameKind::kControl);
  EXPECT_EQ(view->control.type, 0x05);
  EXPECT_EQ(std::vector<std::uint8_t>(view->control.payload.begin(), view->control.payload.end()), payload);
}

TEST(MuxCodecTests, DecodeViewHeartbeatFrame) {
  std::vector<std::uint8_t> payload{0xAA, 0xBB};
  auto frame = mux::make_heartbeat_frame(123456789, 42, payload);
  auto encoded = mux::MuxCodec::encode(frame);

  auto view = mux::MuxCodec::decode_view(encoded);

  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(view->kind, mux::FrameKind::kHeartbeat);
  EXPECT_EQ(view->heartbeat.timestamp, 123456789U);
  EXPECT_EQ(view->heartbeat.sequence, 42U);
  EXPECT_EQ(std::vector<std::uint8_t>(view->heartbeat.payload.begin(), view->heartbeat.payload.end()), payload);
}

TEST(MuxCodecTests, DecodeViewRejectsEmptyData) {
  auto view = mux::MuxCodec::decode_view({});
  EXPECT_FALSE(view.has_value());
}

TEST(MuxCodecTests, DecodeViewRejectsTruncatedFrame) {
  std::vector<std::uint8_t> payload{0x01, 0x02};
  auto frame = mux::make_data_frame(1, 1, false, payload);
  auto encoded = mux::MuxCodec::encode(frame);
  encoded.pop_back();

  auto view = mux::MuxCodec::decode_view(encoded);
  EXPECT_FALSE(view.has_value());
}

TEST(MuxCodecTests, DecodeViewPayloadIsViewIntoBuffer) {
  std::vector<std::uint8_t> payload{0x01, 0x02, 0x03, 0x04};
  auto frame = mux::make_data_frame(42, 100, false, payload);
  auto encoded = mux::MuxCodec::encode(frame);

  auto view = mux::MuxCodec::decode_view(encoded);
  ASSERT_TRUE(view.has_value());

  // The payload span should point into the encoded buffer (zero-copy)
  const auto payload_start = encoded.data() + mux::MuxCodec::kDataHeaderSize;
  EXPECT_EQ(view->data.payload.data(), payload_start);
}

TEST(MuxCodecTests, EncodedSizeViewMatchesEncodedSize) {
  std::vector<std::uint8_t> payload{1, 2, 3, 4, 5};
  auto data_frame = mux::make_data_frame(1, 1, true, payload);
  auto encoded = mux::MuxCodec::encode(data_frame);
  auto view = mux::MuxCodec::decode_view(encoded);
  ASSERT_TRUE(view.has_value());
  EXPECT_EQ(mux::MuxCodec::encoded_size_view(*view), mux::MuxCodec::encoded_size(data_frame));
}

TEST(MuxCodecTests, EncodeViewToRoundTrip) {
  std::vector<std::uint8_t> payload{0x01, 0x02, 0x03, 0x04};
  auto frame = mux::make_data_frame(42, 100, true, payload);
  auto encoded = mux::MuxCodec::encode(frame);

  // Decode to view
  auto view = mux::MuxCodec::decode_view(encoded);
  ASSERT_TRUE(view.has_value());

  // Encode view to new buffer
  std::vector<std::uint8_t> buffer(mux::MuxCodec::encoded_size_view(*view));
  auto size = mux::MuxCodec::encode_view_to(*view, buffer);
  EXPECT_EQ(size, buffer.size());

  // Should match original encoding
  EXPECT_EQ(buffer, encoded);
}

}  // namespace veil::tests
