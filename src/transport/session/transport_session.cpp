#include "transport/session/transport_session.h"

#include <sodium.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "common/crypto/crypto_engine.h"
#include "common/crypto/random.h"
#include "common/logging/logger.h"

// SECURITY: Nonce overflow threshold.
// With uint64_t, we can send 2^64 packets before overflow. At 10 Gbps with 1KB packets,
// this would take over 58 million years. However, we set a conservative threshold to
// detect any anomalies or implementation bugs that might cause rapid sequence growth.
// This threshold triggers a warning well before any practical risk of overflow.
constexpr std::uint64_t kNonceOverflowWarningThreshold = std::numeric_limits<std::uint64_t>::max() - (1ULL << 32);

namespace veil::transport {

TransportSession::TransportSession(const handshake::HandshakeSession& handshake_session,
                                   TransportSessionConfig config, std::function<TimePoint()> now_fn)
    : config_(config),
      now_fn_(std::move(now_fn)),
      keys_(handshake_session.keys),
      current_session_id_(handshake_session.session_id),
      send_seq_obfuscation_key_(crypto::derive_sequence_obfuscation_key(keys_.send_key, keys_.send_nonce)),
      recv_seq_obfuscation_key_(crypto::derive_sequence_obfuscation_key(keys_.recv_key, keys_.recv_nonce)),
      replay_window_(config_.replay_window_size),
      session_rotator_(config_.session_rotation_interval, config_.session_rotation_packets),
      reorder_buffer_(0, config_.reorder_buffer_size),
      fragment_reassembly_(config_.fragment_buffer_size),
      retransmit_buffer_(config_.retransmit_config, now_fn_) {
  // Enhanced diagnostic logging for session creation (Issue #69, #72)
  // Use INFO level so key fingerprints are always logged, not just in verbose mode
  // This helps diagnose key mismatch issues between client and server
  LOG_INFO("TransportSession created: session_id={}", current_session_id_);
  LOG_INFO("  send_key_fp={:02x}{:02x}{:02x}{:02x}, send_nonce_fp={:02x}{:02x}{:02x}{:02x}",
           keys_.send_key[0], keys_.send_key[1], keys_.send_key[2], keys_.send_key[3],
           keys_.send_nonce[0], keys_.send_nonce[1], keys_.send_nonce[2], keys_.send_nonce[3]);
  LOG_INFO("  recv_key_fp={:02x}{:02x}{:02x}{:02x}, recv_nonce_fp={:02x}{:02x}{:02x}{:02x}",
           keys_.recv_key[0], keys_.recv_key[1], keys_.recv_key[2], keys_.recv_key[3],
           keys_.recv_nonce[0], keys_.recv_nonce[1], keys_.recv_nonce[2], keys_.recv_nonce[3]);
  LOG_INFO("  send_seq_obfuscation_key_fp={:02x}{:02x}{:02x}{:02x}, recv_seq_obfuscation_key_fp={:02x}{:02x}{:02x}{:02x}",
           send_seq_obfuscation_key_[0], send_seq_obfuscation_key_[1],
           send_seq_obfuscation_key_[2], send_seq_obfuscation_key_[3],
           recv_seq_obfuscation_key_[0], recv_seq_obfuscation_key_[1],
           recv_seq_obfuscation_key_[2], recv_seq_obfuscation_key_[3]);
}

TransportSession::~TransportSession() {
  // SECURITY: Clear all session key material on destruction
  sodium_memzero(keys_.send_key.data(), keys_.send_key.size());
  sodium_memzero(keys_.recv_key.data(), keys_.recv_key.size());
  sodium_memzero(keys_.send_nonce.data(), keys_.send_nonce.size());
  sodium_memzero(keys_.recv_nonce.data(), keys_.recv_nonce.size());
  sodium_memzero(send_seq_obfuscation_key_.data(), send_seq_obfuscation_key_.size());
  sodium_memzero(recv_seq_obfuscation_key_.data(), recv_seq_obfuscation_key_.size());
  LOG_DEBUG("TransportSession destroyed, keys cleared");
}

std::vector<std::vector<std::uint8_t>> TransportSession::encrypt_data(
    std::span<const std::uint8_t> plaintext, std::uint64_t stream_id, bool fin) {
  VEIL_DCHECK_THREAD(thread_checker_);

  std::vector<std::vector<std::uint8_t>> result;

  // Fragment data if necessary.
  auto frames = fragment_data(plaintext, stream_id, fin);

  // PERFORMANCE (Issue #94): Pre-allocate result vector to avoid reallocations.
  result.reserve(frames.size());

  for (auto& frame : frames) {
    auto encrypted = build_encrypted_packet(frame);

    // Store in retransmit buffer.
    if (retransmit_buffer_.has_capacity(encrypted.size())) {
      retransmit_buffer_.insert(send_sequence_ - 1, encrypted);
    }

    ++stats_.packets_sent;
    stats_.bytes_sent += encrypted.size();
    if (frame.kind == mux::FrameKind::kData) {
      ++stats_.fragments_sent;
    }

    result.push_back(std::move(encrypted));
    ++packets_since_rotation_;
  }

  return result;
}

std::vector<std::uint8_t> TransportSession::encrypt_frame(const mux::MuxFrame& frame) {
  VEIL_DCHECK_THREAD(thread_checker_);

  // Use the existing build_encrypted_packet method which properly encrypts any frame type.
  // This preserves the frame's original kind (ACK, control, heartbeat, etc.) without wrapping
  // it in a DATA frame like encrypt_data() does.
  auto encrypted = build_encrypted_packet(frame);

  // Update statistics for non-data frames (data frame stats are updated in encrypt_data)
  ++stats_.packets_sent;
  stats_.bytes_sent += encrypted.size();
  ++packets_since_rotation_;

  return encrypted;
}

std::optional<std::vector<mux::MuxFrame>> TransportSession::decrypt_packet(
    std::span<const std::uint8_t> ciphertext) {
  VEIL_DCHECK_THREAD(thread_checker_);

  // Minimum packet size: nonce (8 bytes for sequence) + tag (16 bytes) + header (1 byte minimum)
  constexpr std::size_t kMinPacketSize = 8 + 16 + 1;
  if (ciphertext.size() < kMinPacketSize) {
    LOG_DEBUG("Packet too small: {} bytes", ciphertext.size());
    ++stats_.packets_dropped_decrypt;
    return std::nullopt;
  }

  // Extract obfuscated sequence from first 8 bytes.
  std::uint64_t obfuscated_sequence = 0;
  for (int i = 0; i < 8; ++i) {
    obfuscated_sequence = (obfuscated_sequence << 8) | ciphertext[static_cast<std::size_t>(i)];
  }

  // DPI RESISTANCE (Issue #21): Deobfuscate sequence number.
  // The sender obfuscated the sequence to prevent traffic analysis. We reverse the
  // obfuscation here to recover the real sequence for nonce derivation and replay checking.
  const std::uint64_t sequence = crypto::deobfuscate_sequence(obfuscated_sequence, recv_seq_obfuscation_key_);

  // Enhanced diagnostic logging for decryption debugging (Issue #69, #72)
  // Changed to DEBUG level to avoid performance impact in hot path (Issue #92)
  LOG_DEBUG("Decrypt attempt: session_id={}, pkt_size={}, obfuscated_seq={:#018x}, deobfuscated_seq={}",
            current_session_id_, ciphertext.size(), obfuscated_sequence, sequence);
  LOG_DEBUG("  recv_seq_obfuscation_key_fp={:02x}{:02x}{:02x}{:02x}, first_8_bytes={:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
            recv_seq_obfuscation_key_[0], recv_seq_obfuscation_key_[1],
            recv_seq_obfuscation_key_[2], recv_seq_obfuscation_key_[3],
            ciphertext[0], ciphertext[1], ciphertext[2], ciphertext[3],
            ciphertext[4], ciphertext[5], ciphertext[6], ciphertext[7]);

  // Replay check.
  if (!replay_window_.mark_and_check(sequence)) {
    LOG_DEBUG("Packet replay detected or out of window: sequence={}, highest={}",
              sequence, replay_window_.highest());
    ++stats_.packets_dropped_replay;
    return std::nullopt;
  }
  LOG_DEBUG("Replay check passed, proceeding to decryption");

  // Derive nonce from sequence.
  const auto nonce = crypto::derive_nonce(keys_.recv_nonce, sequence);

  // Decrypt (skip sequence prefix).
  auto ciphertext_body = ciphertext.subspan(8);
  auto decrypted = crypto::aead_decrypt(keys_.recv_key, nonce, {}, ciphertext_body);
  if (!decrypted) {
    // Enhanced error logging for decryption failures (Issue #69, #72)
    // Changed to DEBUG level to avoid performance impact in hot path (Issue #92)
    // Log key fingerprints (first 4 bytes) to help diagnose key mismatch issues
    LOG_DEBUG("Decryption FAILED: session_id={}, sequence={}, ciphertext_size={}, "
              "recv_key_fp={:02x}{:02x}{:02x}{:02x}, recv_nonce_fp={:02x}{:02x}{:02x}{:02x}",
              current_session_id_, sequence, ciphertext_body.size(),
              keys_.recv_key[0], keys_.recv_key[1], keys_.recv_key[2], keys_.recv_key[3],
              keys_.recv_nonce[0], keys_.recv_nonce[1], keys_.recv_nonce[2], keys_.recv_nonce[3]);
    // Also log the obfuscation key fingerprint and packet header
    LOG_DEBUG("  recv_seq_obfuscation_key_fp={:02x}{:02x}{:02x}{:02x}, first_pkt_bytes={:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
              recv_seq_obfuscation_key_[0], recv_seq_obfuscation_key_[1],
              recv_seq_obfuscation_key_[2], recv_seq_obfuscation_key_[3],
              ciphertext[0], ciphertext[1], ciphertext[2], ciphertext[3],
              ciphertext[4], ciphertext[5], ciphertext[6], ciphertext[7]);

    // Issue #78: Unmark sequence in replay window to allow legitimate retransmission
    // If decryption fails (e.g., due to wrong session keys after session rotation),
    // we should allow the server to retransmit this packet rather than permanently
    // rejecting it as a replay.
    replay_window_.unmark(sequence);
    LOG_DEBUG("  Unmarked sequence {} in replay window to allow retransmission", sequence);

    ++stats_.packets_dropped_decrypt;
    return std::nullopt;
  }

  // Enhanced diagnostic logging for decryption success (Issue #72)
  // Changed to DEBUG level to avoid performance impact in hot path (Issue #92)
  LOG_DEBUG("Decryption SUCCESS: session_id={}, sequence={}, decrypted_size={}",
            current_session_id_, sequence, decrypted->size());

  ++stats_.packets_received;
  stats_.bytes_received += ciphertext.size();

  // Parse mux frames from decrypted data.
  std::vector<mux::MuxFrame> frames;
  auto frame = mux::MuxCodec::decode(*decrypted);
  if (frame) {
    // Log frame details for debugging (Issue #72)
    LOG_WARN("  Frame decoded: kind={}, payload_size={}",
             static_cast<int>(frame->kind),
             frame->kind == mux::FrameKind::kData ? frame->data.payload.size() : 0);

    if (frame->kind == mux::FrameKind::kData) {
      ++stats_.fragments_received;
      recv_ack_bitmap_.ack(sequence);

      // Issue #74: Fragment reassembly
      // For fragmented messages, sequence is encoded as (msg_id << 32) | frag_idx.
      // For non-fragmented messages (or first fragment of msg_id=0), we detect by fin flag.
      // - If fin=true: complete message, return directly
      // - If fin=false: fragment, accumulate and try reassembly
      const std::uint64_t frame_seq = frame->data.sequence;
      const std::uint64_t msg_id = frame_seq >> 32;
      const std::uint32_t frag_idx = static_cast<std::uint32_t>(frame_seq & 0xFFFFFFFF);

      // Determine if this is a fragment vs complete message:
      // Issue #74: The sender uses msg_id >= 1 for fragmented messages, encoding as (msg_id << 32) | frag_idx.
      // Non-fragmented messages use raw sequence numbers (0, 1, 2, ...) which fit in 32 bits.
      // We detect fragments by checking if the sequence exceeds 32-bit range (upper 32 bits non-zero).
      // This is equivalent to checking msg_id > 0, but more explicit about the encoding.
      const std::uint64_t reassembly_id = msg_id;  // Use msg_id as reassembly key
      const bool is_fragment = (frame_seq > 0xFFFFFFFF);

      if (is_fragment) {
        // This is a fragment - push to reassembly buffer using msg_id as the key

        // Calculate offset from fragment index
        // We track cumulative size per message to compute offsets
        // For simplicity, use frag_idx as offset (works when fragments arrive in order)
        // TODO: For out-of-order fragments, we'd need more sophisticated tracking
        mux::Fragment frag{
            .offset = static_cast<std::uint16_t>(frag_idx * config_.max_fragment_size),
            .data = std::move(frame->data.payload),
            .last = frame->data.fin};

        LOG_WARN("  Fragment: msg_id={}, frag_idx={}, offset={}, size={}, last={}",
                 msg_id, frag_idx, frag.offset, frag.data.size(), frag.last);

        fragment_reassembly_.push(reassembly_id, std::move(frag), now_fn_());

        // Try to reassemble the complete message
        auto reassembled = fragment_reassembly_.try_reassemble(reassembly_id);
        if (reassembled) {
          // Successfully reassembled - create a new data frame with complete payload
          LOG_WARN("  Reassembled complete message: msg_id={}, size={}", msg_id, reassembled->size());
          ++stats_.messages_reassembled;

          mux::MuxFrame complete_frame{};
          complete_frame.kind = mux::FrameKind::kData;
          complete_frame.data.stream_id = frame->data.stream_id;
          complete_frame.data.sequence = frame_seq;  // Use original sequence
          complete_frame.data.fin = true;
          complete_frame.data.payload = std::move(*reassembled);
          frames.push_back(std::move(complete_frame));
        }
        // If not yet complete, don't add to frames - wait for more fragments
      } else {
        // Complete non-fragmented message - return directly
        LOG_WARN("  Complete message: sequence={}, size={}", frame_seq, frame->data.payload.size());
        frames.push_back(std::move(*frame));
      }
    } else {
      // Non-data frames (ACK, control, heartbeat) - return directly
      frames.push_back(std::move(*frame));
    }
  } else {
    // Log frame decode failure for debugging (Issue #72)
    LOG_WARN("  Frame decode FAILED: decrypted_size={}, first_byte={:#04x}",
             decrypted->size(), decrypted->empty() ? 0 : (*decrypted)[0]);
  }

  if (sequence > recv_sequence_max_) {
    recv_sequence_max_ = sequence;
  }

  return frames;
}

std::vector<std::vector<std::uint8_t>> TransportSession::get_retransmit_packets() {
  VEIL_DCHECK_THREAD(thread_checker_);

  std::vector<std::vector<std::uint8_t>> result;
  auto to_retransmit = retransmit_buffer_.get_packets_to_retransmit();

  // PERFORMANCE (Issue #94): Pre-allocate result vector to avoid reallocations.
  result.reserve(to_retransmit.size());

  for (const auto* pkt : to_retransmit) {
    if (retransmit_buffer_.mark_retransmitted(pkt->sequence)) {
      result.push_back(pkt->data);
      ++stats_.retransmits;
    } else {
      // Exceeded max retries, drop packet.
      retransmit_buffer_.drop_packet(pkt->sequence);
    }
  }

  return result;
}

void TransportSession::process_ack(const mux::AckFrame& ack) {
  VEIL_DCHECK_THREAD(thread_checker_);

  // Debug logging for ACK processing (Issue #72)
  // Changed to DEBUG level to avoid performance impact in hot path (Issue #92)
  LOG_DEBUG("process_ack called: stream_id={}, ack={}, bitmap={:#010x}, pending_before={}",
            ack.stream_id, ack.ack, ack.bitmap, retransmit_buffer_.pending_count());

  // Cumulative ACK.
  retransmit_buffer_.acknowledge_cumulative(ack.ack);

  // Selective ACK from bitmap.
  for (std::uint32_t i = 0; i < 32; ++i) {
    if (((ack.bitmap >> i) & 1U) != 0U) {
      std::uint64_t seq = ack.ack - 1 - i;
      if (seq > 0) {
        retransmit_buffer_.acknowledge(seq);
      }
    }
  }

  // Debug logging for ACK processing result (Issue #72)
  // Changed to DEBUG level to avoid performance impact in hot path (Issue #92)
  LOG_DEBUG("process_ack done: pending_after={}", retransmit_buffer_.pending_count());
}

mux::AckFrame TransportSession::generate_ack(std::uint64_t stream_id) {
  VEIL_DCHECK_THREAD(thread_checker_);

  return mux::AckFrame{
      .stream_id = stream_id,
      .ack = recv_ack_bitmap_.head(),
      .bitmap = recv_ack_bitmap_.bitmap(),
  };
}

bool TransportSession::should_rotate_session() {
  VEIL_DCHECK_THREAD(thread_checker_);
  return session_rotator_.should_rotate(packets_since_rotation_, now_fn_());
}

void TransportSession::rotate_session() {
  VEIL_DCHECK_THREAD(thread_checker_);

  // SECURITY: Capture sequence number before rotation for verification
  [[maybe_unused]] const auto sequence_before_rotation = send_sequence_;

  current_session_id_ = session_rotator_.rotate(now_fn_());
  packets_since_rotation_ = 0;
  ++stats_.session_rotations;

  // ===========================================================================
  // SECURITY-CRITICAL: NONCE COUNTER LIFECYCLE
  // ===========================================================================
  // The nonce for ChaCha20-Poly1305 is derived as:
  //   nonce = derive_nonce(base_nonce, send_sequence_)
  //
  // Where derive_nonce XORs the counter into the last 8 bytes of base_nonce.
  //
  // CRITICAL INVARIANT: send_sequence_ MUST NEVER be reset.
  //
  // Why this matters:
  // - ChaCha20-Poly1305 security completely breaks if the same (key, nonce) pair
  //   is ever used twice
  // - The encryption key (keys_.send_key) is derived once during handshake and
  //   does NOT change during session rotation
  // - Session rotation only changes the session_id for protocol-level management
  //
  // Nonce uniqueness guarantee:
  // - send_sequence_ is uint64_t, allowing 2^64 unique nonces
  // - At 10 Gbps with 1KB packets, exhaustion would take ~58 million years
  // - send_sequence_ is incremented after each packet in build_encrypted_packet()
  // - It is NEVER reset or decremented
  //
  // This design was chosen over alternatives like:
  // - Rotating keys on session rotation: Would require re-handshake or key derivation
  // - Resetting counter with new base_nonce: Adds complexity, risk of implementation bugs
  // - Using random nonces: Requires tracking to prevent collisions (birthday bound)
  //
  // See also: Issue #3 - Verify nonce counter lifecycle on session rotation
  // ===========================================================================

  // ASSERTION: Verify send_sequence_ was not reset (defense in depth)
  assert(send_sequence_ == sequence_before_rotation &&
         "SECURITY VIOLATION: send_sequence_ must never be reset during rotation");

  LOG_DEBUG("Session rotated to session_id={}, send_sequence_={} (unchanged)",
            current_session_id_, send_sequence_);
}

std::vector<std::uint8_t> TransportSession::build_encrypted_packet(const mux::MuxFrame& frame) {
  // SECURITY: Check for sequence number overflow (extremely unlikely but provides defense in depth)
  // At 10 Gbps with 1KB packets, reaching this threshold would take millions of years,
  // but we check anyway to catch any implementation bugs that might cause unexpected growth.
  if (send_sequence_ >= kNonceOverflowWarningThreshold) {
    LOG_ERROR("SECURITY WARNING: send_sequence_ approaching overflow (current={}). "
              "Session should be re-established to prevent nonce reuse.",
              send_sequence_);
    // Note: We log but continue - in practice this is unreachable under normal operation.
    // A production system might want to force session termination here.
  }

  // Serialize the frame.
  auto plaintext = mux::MuxCodec::encode(frame);

  // Derive nonce from current send sequence.
  // SECURITY: Each packet gets a unique nonce = base_nonce XOR send_sequence_
  // Since send_sequence_ is never reset and always increments, nonces are guaranteed unique.
  const auto nonce = crypto::derive_nonce(keys_.send_nonce, send_sequence_);

  // Encrypt using ChaCha20-Poly1305 AEAD.
  auto ciphertext = crypto::aead_encrypt(keys_.send_key, nonce, {}, plaintext);

  // DPI RESISTANCE (Issue #21): Obfuscate sequence number before transmission.
  // Previously, the sequence was sent in plaintext, creating a DPI signature (monotonically
  // increasing values). Now we obfuscate it using ChaCha20 with a session-specific key.
  // The receiver can deobfuscate using the same key to recover the sequence for nonce derivation.
  const std::uint64_t obfuscated_sequence = crypto::obfuscate_sequence(send_sequence_, send_seq_obfuscation_key_);

  // Enhanced diagnostic logging for encryption (Issue #69)
  // Log key fingerprints (first 4 bytes) to help diagnose key mismatch between client and server
  LOG_DEBUG("Encrypt: session_id={}, sequence={}, obfuscated_seq={:#018x}, plaintext_size={}, "
            "send_key_fp={:02x}{:02x}{:02x}{:02x}, send_nonce_fp={:02x}{:02x}{:02x}{:02x}",
            current_session_id_, send_sequence_, obfuscated_sequence, plaintext.size(),
            keys_.send_key[0], keys_.send_key[1], keys_.send_key[2], keys_.send_key[3],
            keys_.send_nonce[0], keys_.send_nonce[1], keys_.send_nonce[2], keys_.send_nonce[3]);
  LOG_DEBUG("  send_seq_obfuscation_key_fp={:02x}{:02x}{:02x}{:02x}",
            send_seq_obfuscation_key_[0], send_seq_obfuscation_key_[1],
            send_seq_obfuscation_key_[2], send_seq_obfuscation_key_[3]);

  // Prepend obfuscated sequence number (8 bytes big-endian).
  std::vector<std::uint8_t> packet;
  packet.reserve(8 + ciphertext.size());
  for (int i = 7; i >= 0; --i) {
    packet.push_back(static_cast<std::uint8_t>((obfuscated_sequence >> (8 * i)) & 0xFF));
  }
  packet.insert(packet.end(), ciphertext.begin(), ciphertext.end());

  // SECURITY: Increment AFTER using the sequence number.
  // This ensures each packet uses a unique sequence, and the next packet will use the next value.
  ++send_sequence_;

  return packet;
}

std::vector<mux::MuxFrame> TransportSession::fragment_data(std::span<const std::uint8_t> data,
                                                            std::uint64_t stream_id, bool fin) {
  // Issue #74: The 'fin' parameter is intentionally ignored. We always set fin=true on the
  // last fragment (or single message) to ensure the receiver can properly detect message
  // completion and perform fragment reassembly. This fixes packet loss caused by fragments
  // not being reassembled before TUN write.
  (void)fin;

  std::vector<mux::MuxFrame> frames;

  // PERFORMANCE (Issue #94): Pre-calculate number of fragments and reserve capacity.
  // This avoids vector reallocations during fragment generation.
  if (data.size() > config_.max_fragment_size) {
    const std::size_t num_fragments = (data.size() + config_.max_fragment_size - 1) / config_.max_fragment_size;
    frames.reserve(num_fragments);
  }

  if (data.size() <= config_.max_fragment_size) {
    // No fragmentation needed. Always set fin=true to indicate complete message.
    // Issue #74: Without fin=true, receiver can't distinguish complete messages from fragments.
    frames.push_back(mux::make_data_frame(
        stream_id, message_id_counter_++, true,
        std::vector<std::uint8_t>(data.begin(), data.end())));
    return frames;
  }

  // Fragment the data.
  // Issue #74: Use (msg_id + 1) to ensure the encoded sequence has msg_id >= 1.
  // This distinguishes fragmented message sequences (e.g., (1<<32)|0 = 4294967296)
  // from non-fragmented message sequences (e.g., 0, 1, 2, ...).
  // Without this, fragment sequences for msg_id=0 would be 0, 1, 2, ... which collide
  // with non-fragmented message sequences.
  const std::uint64_t msg_id = message_id_counter_++ + 1;
  std::size_t offset = 0;
  std::uint64_t frag_seq = 0;

  while (offset < data.size()) {
    const std::size_t chunk_size = std::min(config_.max_fragment_size, data.size() - offset);
    const bool is_last = (offset + chunk_size >= data.size());
    // Issue #74: Always set fin=true on last fragment so receiver can detect message completion.
    // This enables proper fragment reassembly regardless of caller's fin parameter.
    const bool frag_fin = is_last;

    std::vector<std::uint8_t> chunk(data.begin() + static_cast<std::ptrdiff_t>(offset),
                                     data.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));

    // For fragmented messages, we use a special encoding in the sequence field.
    // High 32 bits: message ID (>= 1), Low 32 bits: fragment index.
    const std::uint64_t encoded_seq = (msg_id << 32) | frag_seq;

    frames.push_back(mux::make_data_frame(stream_id, encoded_seq, frag_fin, std::move(chunk)));

    offset += chunk_size;
    ++frag_seq;
  }

  return frames;
}

}  // namespace veil::transport
