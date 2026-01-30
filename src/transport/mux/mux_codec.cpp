#include "transport/mux/mux_codec.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace {

void write_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void write_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
  }
}

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
  return static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
}

std::uint32_t read_u32(std::span<const std::uint8_t> data, std::size_t offset) {
  return (static_cast<std::uint32_t>(data[offset]) << 24) |
         (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
         static_cast<std::uint32_t>(data[offset + 3]);
}

std::uint64_t read_u64(std::span<const std::uint8_t> data, std::size_t offset) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | data[offset + static_cast<std::size_t>(i)];
  }
  return value;
}

}  // namespace

namespace veil::mux {

std::vector<std::uint8_t> MuxCodec::encode(const MuxFrame& frame) {
  std::vector<std::uint8_t> out;
  out.reserve(encoded_size(frame));
  out.push_back(static_cast<std::uint8_t>(frame.kind));

  switch (frame.kind) {
    case FrameKind::kData: {
      write_u64(out, frame.data.stream_id);
      write_u64(out, frame.data.sequence);
      std::uint8_t flags = frame.data.fin ? 0x01 : 0x00;
      out.push_back(flags);
      write_u16(out, static_cast<std::uint16_t>(frame.data.payload.size()));
      out.insert(out.end(), frame.data.payload.begin(), frame.data.payload.end());
      break;
    }
    case FrameKind::kAck: {
      write_u64(out, frame.ack.stream_id);
      write_u64(out, frame.ack.ack);
      write_u32(out, frame.ack.bitmap);
      break;
    }
    case FrameKind::kControl: {
      out.push_back(frame.control.type);
      write_u16(out, static_cast<std::uint16_t>(frame.control.payload.size()));
      out.insert(out.end(), frame.control.payload.begin(), frame.control.payload.end());
      break;
    }
    case FrameKind::kHeartbeat: {
      write_u64(out, frame.heartbeat.timestamp);
      write_u64(out, frame.heartbeat.sequence);
      write_u16(out, static_cast<std::uint16_t>(frame.heartbeat.payload.size()));
      out.insert(out.end(), frame.heartbeat.payload.begin(), frame.heartbeat.payload.end());
      break;
    }
  }

  return out;
}

std::optional<MuxFrame> MuxCodec::decode(std::span<const std::uint8_t> data) {
  if (data.empty()) {
    return std::nullopt;
  }

  MuxFrame frame{};
  const auto kind = static_cast<FrameKind>(data[0]);
  frame.kind = kind;

  switch (kind) {
    case FrameKind::kData: {
      if (data.size() < kDataHeaderSize) {
        return std::nullopt;
      }
      frame.data.stream_id = read_u64(data, 1);
      frame.data.sequence = read_u64(data, 9);
      std::uint8_t flags = data[17];
      frame.data.fin = (flags & 0x01) != 0;
      std::uint16_t payload_len = read_u16(data, 18);
      if (data.size() != kDataHeaderSize + payload_len) {
        return std::nullopt;
      }
      frame.data.payload.assign(data.begin() + kDataHeaderSize, data.end());
      break;
    }
    case FrameKind::kAck: {
      if (data.size() != kAckSize) {
        return std::nullopt;
      }
      frame.ack.stream_id = read_u64(data, 1);
      frame.ack.ack = read_u64(data, 9);
      frame.ack.bitmap = read_u32(data, 17);
      break;
    }
    case FrameKind::kControl: {
      if (data.size() < kControlHeaderSize) {
        return std::nullopt;
      }
      frame.control.type = data[1];
      std::uint16_t payload_len = read_u16(data, 2);
      if (data.size() != kControlHeaderSize + payload_len) {
        return std::nullopt;
      }
      frame.control.payload.assign(data.begin() + kControlHeaderSize, data.end());
      break;
    }
    case FrameKind::kHeartbeat: {
      if (data.size() < kHeartbeatHeaderSize) {
        return std::nullopt;
      }
      frame.heartbeat.timestamp = read_u64(data, 1);
      frame.heartbeat.sequence = read_u64(data, 9);
      std::uint16_t payload_len = read_u16(data, 17);
      if (data.size() != kHeartbeatHeaderSize + payload_len) {
        return std::nullopt;
      }
      frame.heartbeat.payload.assign(data.begin() + kHeartbeatHeaderSize, data.end());
      break;
    }
    default:
      return std::nullopt;
  }

  return frame;
}

std::size_t MuxCodec::encoded_size(const MuxFrame& frame) {
  switch (frame.kind) {
    case FrameKind::kData:
      return kDataHeaderSize + frame.data.payload.size();
    case FrameKind::kAck:
      return kAckSize;
    case FrameKind::kControl:
      return kControlHeaderSize + frame.control.payload.size();
    case FrameKind::kHeartbeat:
      return kHeartbeatHeaderSize + frame.heartbeat.payload.size();
  }
  return 0;
}

MuxFrame make_data_frame(std::uint64_t stream_id, std::uint64_t sequence, bool fin,
                         std::vector<std::uint8_t> payload) {
  MuxFrame frame{};
  frame.kind = FrameKind::kData;
  frame.data.stream_id = stream_id;
  frame.data.sequence = sequence;
  frame.data.fin = fin;
  frame.data.payload = std::move(payload);
  return frame;
}

MuxFrame make_ack_frame(std::uint64_t stream_id, std::uint64_t ack, std::uint32_t bitmap) {
  MuxFrame frame{};
  frame.kind = FrameKind::kAck;
  frame.ack.stream_id = stream_id;
  frame.ack.ack = ack;
  frame.ack.bitmap = bitmap;
  return frame;
}

MuxFrame make_control_frame(std::uint8_t type, std::vector<std::uint8_t> payload) {
  MuxFrame frame{};
  frame.kind = FrameKind::kControl;
  frame.control.type = type;
  frame.control.payload = std::move(payload);
  return frame;
}

MuxFrame make_heartbeat_frame(std::uint64_t timestamp, std::uint64_t sequence,
                               std::vector<std::uint8_t> payload) {
  MuxFrame frame{};
  frame.kind = FrameKind::kHeartbeat;
  frame.heartbeat.timestamp = timestamp;
  frame.heartbeat.sequence = sequence;
  frame.heartbeat.payload = std::move(payload);
  return frame;
}

// PERFORMANCE (Issue #97): Zero-copy encode/decode implementations.

namespace {

// Write helpers for zero-copy encoding.
void write_u16_at(std::span<std::uint8_t> out, std::size_t offset, std::uint16_t value) {
  out[offset] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  out[offset + 1] = static_cast<std::uint8_t>(value & 0xFF);
}

void write_u32_at(std::span<std::uint8_t> out, std::size_t offset, std::uint32_t value) {
  out[offset] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
  out[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  out[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  out[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

void write_u64_at(std::span<std::uint8_t> out, std::size_t offset, std::uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    out[offset + static_cast<std::size_t>(7 - i)] =
        static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
  }
}

}  // namespace

std::size_t MuxCodec::encode_to(const MuxFrame& frame, std::span<std::uint8_t> output) {
  const std::size_t required = encoded_size(frame);
  if (output.size() < required) {
    return 0;  // Buffer too small
  }

  std::size_t pos = 0;
  output[pos++] = static_cast<std::uint8_t>(frame.kind);

  switch (frame.kind) {
    case FrameKind::kData: {
      write_u64_at(output, pos, frame.data.stream_id);
      pos += 8;
      write_u64_at(output, pos, frame.data.sequence);
      pos += 8;
      output[pos++] = frame.data.fin ? 0x01 : 0x00;
      write_u16_at(output, pos, static_cast<std::uint16_t>(frame.data.payload.size()));
      pos += 2;
      std::copy(frame.data.payload.begin(), frame.data.payload.end(), output.begin() + static_cast<std::ptrdiff_t>(pos));
      pos += frame.data.payload.size();
      break;
    }
    case FrameKind::kAck: {
      write_u64_at(output, pos, frame.ack.stream_id);
      pos += 8;
      write_u64_at(output, pos, frame.ack.ack);
      pos += 8;
      write_u32_at(output, pos, frame.ack.bitmap);
      pos += 4;
      break;
    }
    case FrameKind::kControl: {
      output[pos++] = frame.control.type;
      write_u16_at(output, pos, static_cast<std::uint16_t>(frame.control.payload.size()));
      pos += 2;
      std::copy(frame.control.payload.begin(), frame.control.payload.end(), output.begin() + static_cast<std::ptrdiff_t>(pos));
      pos += frame.control.payload.size();
      break;
    }
    case FrameKind::kHeartbeat: {
      write_u64_at(output, pos, frame.heartbeat.timestamp);
      pos += 8;
      write_u64_at(output, pos, frame.heartbeat.sequence);
      pos += 8;
      write_u16_at(output, pos, static_cast<std::uint16_t>(frame.heartbeat.payload.size()));
      pos += 2;
      std::copy(frame.heartbeat.payload.begin(), frame.heartbeat.payload.end(), output.begin() + static_cast<std::ptrdiff_t>(pos));
      pos += frame.heartbeat.payload.size();
      break;
    }
  }

  return pos;
}

std::optional<MuxFrameView> MuxCodec::decode_view(std::span<const std::uint8_t> data) {
  if (data.empty()) {
    return std::nullopt;
  }

  MuxFrameView frame{};
  const auto kind = static_cast<FrameKind>(data[0]);
  frame.kind = kind;

  switch (kind) {
    case FrameKind::kData: {
      if (data.size() < kDataHeaderSize) {
        return std::nullopt;
      }
      frame.data.stream_id = read_u64(data, 1);
      frame.data.sequence = read_u64(data, 9);
      std::uint8_t flags = data[17];
      frame.data.fin = (flags & 0x01) != 0;
      std::uint16_t payload_len = read_u16(data, 18);
      if (data.size() != kDataHeaderSize + payload_len) {
        return std::nullopt;
      }
      // Zero-copy: create a span view into the source buffer
      frame.data.payload = data.subspan(kDataHeaderSize, payload_len);
      break;
    }
    case FrameKind::kAck: {
      if (data.size() != kAckSize) {
        return std::nullopt;
      }
      frame.ack.stream_id = read_u64(data, 1);
      frame.ack.ack = read_u64(data, 9);
      frame.ack.bitmap = read_u32(data, 17);
      break;
    }
    case FrameKind::kControl: {
      if (data.size() < kControlHeaderSize) {
        return std::nullopt;
      }
      frame.control.type = data[1];
      std::uint16_t payload_len = read_u16(data, 2);
      if (data.size() != kControlHeaderSize + payload_len) {
        return std::nullopt;
      }
      // Zero-copy: create a span view into the source buffer
      frame.control.payload = data.subspan(kControlHeaderSize, payload_len);
      break;
    }
    case FrameKind::kHeartbeat: {
      if (data.size() < kHeartbeatHeaderSize) {
        return std::nullopt;
      }
      frame.heartbeat.timestamp = read_u64(data, 1);
      frame.heartbeat.sequence = read_u64(data, 9);
      std::uint16_t payload_len = read_u16(data, 17);
      if (data.size() != kHeartbeatHeaderSize + payload_len) {
        return std::nullopt;
      }
      // Zero-copy: create a span view into the source buffer
      frame.heartbeat.payload = data.subspan(kHeartbeatHeaderSize, payload_len);
      break;
    }
    default:
      return std::nullopt;
  }

  return frame;
}

std::size_t MuxCodec::encoded_size_view(const MuxFrameView& frame) {
  switch (frame.kind) {
    case FrameKind::kData:
      return kDataHeaderSize + frame.data.payload.size();
    case FrameKind::kAck:
      return kAckSize;
    case FrameKind::kControl:
      return kControlHeaderSize + frame.control.payload.size();
    case FrameKind::kHeartbeat:
      return kHeartbeatHeaderSize + frame.heartbeat.payload.size();
  }
  return 0;
}

std::size_t MuxCodec::encode_view_to(const MuxFrameView& frame, std::span<std::uint8_t> output) {
  const std::size_t required = encoded_size_view(frame);
  if (output.size() < required) {
    return 0;  // Buffer too small
  }

  std::size_t pos = 0;
  output[pos++] = static_cast<std::uint8_t>(frame.kind);

  switch (frame.kind) {
    case FrameKind::kData: {
      write_u64_at(output, pos, frame.data.stream_id);
      pos += 8;
      write_u64_at(output, pos, frame.data.sequence);
      pos += 8;
      output[pos++] = frame.data.fin ? 0x01 : 0x00;
      write_u16_at(output, pos, static_cast<std::uint16_t>(frame.data.payload.size()));
      pos += 2;
      std::copy(frame.data.payload.begin(), frame.data.payload.end(), output.begin() + static_cast<std::ptrdiff_t>(pos));
      pos += frame.data.payload.size();
      break;
    }
    case FrameKind::kAck: {
      write_u64_at(output, pos, frame.ack.stream_id);
      pos += 8;
      write_u64_at(output, pos, frame.ack.ack);
      pos += 8;
      write_u32_at(output, pos, frame.ack.bitmap);
      pos += 4;
      break;
    }
    case FrameKind::kControl: {
      output[pos++] = frame.control.type;
      write_u16_at(output, pos, static_cast<std::uint16_t>(frame.control.payload.size()));
      pos += 2;
      std::copy(frame.control.payload.begin(), frame.control.payload.end(), output.begin() + static_cast<std::ptrdiff_t>(pos));
      pos += frame.control.payload.size();
      break;
    }
    case FrameKind::kHeartbeat: {
      write_u64_at(output, pos, frame.heartbeat.timestamp);
      pos += 8;
      write_u64_at(output, pos, frame.heartbeat.sequence);
      pos += 8;
      write_u16_at(output, pos, static_cast<std::uint16_t>(frame.heartbeat.payload.size()));
      pos += 2;
      std::copy(frame.heartbeat.payload.begin(), frame.heartbeat.payload.end(), output.begin() + static_cast<std::ptrdiff_t>(pos));
      pos += frame.heartbeat.payload.size();
      break;
    }
  }

  return pos;
}

}  // namespace veil::mux
