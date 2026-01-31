#include "transport/pipeline/pipeline_processor.h"

#include <chrono>
#include <thread>

#include "common/logging/logger.h"
#include "transport/session/transport_session.h"

namespace veil::transport {

PipelineProcessor::PipelineProcessor(TransportSession* session, PipelineConfig config)
    : config_(config),
      session_(session),
      rx_queue_(std::make_unique<utils::SpscQueue<PipelinePacket>>(config.rx_queue_capacity)),
      tx_queue_(std::make_unique<utils::SpscQueue<ProcessedPacket>>(config.tx_queue_capacity)),
      process_worker_(std::make_unique<utils::DedicatedWorker>("Pipeline-Process")),
      tx_worker_(std::make_unique<utils::DedicatedWorker>("Pipeline-TX")) {
  LOG_DEBUG("PipelineProcessor created: rx_queue_capacity={}, tx_queue_capacity={}",
            config.rx_queue_capacity, config.tx_queue_capacity);
}

PipelineProcessor::~PipelineProcessor() {
  stop();
  LOG_DEBUG("PipelineProcessor destroyed");
}

bool PipelineProcessor::start(RxCallback on_rx,
                               TxCompleteCallback on_tx_complete,
                               ErrorCallback on_error) {
  if (running_.load()) {
    LOG_WARN("PipelineProcessor already running");
    return false;
  }

  if (session_ == nullptr) {
    LOG_ERROR("PipelineProcessor: session is null");
    return false;
  }

  on_rx_ = std::move(on_rx);
  on_tx_complete_ = std::move(on_tx_complete);
  on_error_ = std::move(on_error);

  running_.store(true);

  // Start process thread
  process_worker_->start([this]() { process_thread_loop(); });

  // Start TX thread
  tx_worker_->start([this]() { tx_thread_loop(); });

  LOG_INFO("PipelineProcessor started with 2 worker threads");
  return true;
}

void PipelineProcessor::stop() {
  if (!running_.load()) {
    return;
  }

  LOG_INFO("PipelineProcessor stopping...");
  running_.store(false);

  // Stop workers (they will exit their loops)
  process_worker_->stop();
  tx_worker_->stop();

  // Wait for threads to finish
  process_worker_->join();
  tx_worker_->join();

  LOG_INFO("PipelineProcessor stopped. Stats: rx={}, tx={}, processed={}, errors={}",
           stats_.rx_packets.load(), stats_.tx_packets.load(),
           stats_.processed_packets.load(), stats_.decrypt_errors.load());
}

bool PipelineProcessor::submit_rx(std::uint64_t session_id,
                                   std::span<const std::uint8_t> data,
                                   const UdpEndpoint& source) {
  if (!running_.load()) {
    return false;
  }

  PipelinePacket packet;
  packet.data.assign(data.begin(), data.end());
  packet.endpoint = source;
  packet.session_id = session_id;
  packet.timestamp = Clock::now();
  packet.outgoing = false;

  if (!rx_queue_->try_push(std::move(packet))) {
    ++stats_.queue_full_drops;
    if (config_.enable_tracing) {
      LOG_WARN("RX queue full, dropping packet");
    }
    return false;
  }

  ++stats_.rx_packets;
  stats_.rx_bytes += data.size();

  // Update max queue size
  auto size = rx_queue_->size_approx();
  auto max = stats_.rx_queue_max_size.load();
  while (size > max && !stats_.rx_queue_max_size.compare_exchange_weak(max, size)) {
    // Retry on failure
  }

  return true;
}

bool PipelineProcessor::submit_tx(std::uint64_t session_id,
                                   std::span<const std::uint8_t> data,
                                   const UdpEndpoint& dest,
                                   [[maybe_unused]] std::uint64_t stream_id) {
  if (!running_.load()) {
    return false;
  }

  // For outgoing packets, we still need to go through the process thread
  // for encryption. Create a pipeline packet with outgoing flag set.
  PipelinePacket packet;
  packet.data.assign(data.begin(), data.end());
  packet.endpoint = dest;
  packet.session_id = session_id;
  packet.timestamp = Clock::now();
  packet.outgoing = true;

  // We reuse rx_queue for both directions - the outgoing flag tells
  // the process thread whether to encrypt or decrypt
  if (!rx_queue_->try_push(std::move(packet))) {
    ++stats_.queue_full_drops;
    if (config_.enable_tracing) {
      LOG_WARN("Queue full for TX, dropping packet");
    }
    return false;
  }

  return true;
}

void PipelineProcessor::process_thread_loop() {
  LOG_DEBUG("Process thread started");

  while (process_worker_->is_running()) {
    // Try to get a packet from the RX queue
    auto maybe_packet = rx_queue_->try_pop();

    if (!maybe_packet) {
      // Queue empty - brief sleep to avoid busy-waiting
      if (rx_queue_->size_approx() < config_.busy_wait_threshold) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
      continue;
    }

    auto& packet = *maybe_packet;
    auto start_time = Clock::now();

    ProcessedPacket result;
    result.endpoint = packet.endpoint;
    result.session_id = packet.session_id;
    result.outgoing = packet.outgoing;

    if (packet.outgoing) {
      // Encrypt outgoing data
      // THREAD-SAFETY (Issue #163): TransportSession is not thread-safe.
      // We must hold session_mutex_ while calling session methods.
      std::lock_guard<std::mutex> lock(session_mutex_);
      auto encrypted = session_->encrypt_data(packet.data);
      result.packets = std::move(encrypted);
      result.success = true;
    } else {
      // Decrypt incoming packet
      // THREAD-SAFETY (Issue #163): TransportSession is not thread-safe.
      // We must hold session_mutex_ while calling session methods.
      std::lock_guard<std::mutex> lock(session_mutex_);
      auto decrypted = session_->decrypt_packet(packet.data);
      if (decrypted) {
        result.frames = std::move(*decrypted);
        result.success = true;
      } else {
        result.success = false;
        ++stats_.decrypt_errors;
      }
    }

    auto process_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - start_time);
    stats_.total_process_time_ns += static_cast<std::uint64_t>(process_time.count());
    ++stats_.processed_packets;

    // For successful incoming packets, invoke the callback directly
    // (we don't need to queue them for TX)
    if (!packet.outgoing && result.success && on_rx_) {
      on_rx_(result.session_id, result.frames, result.endpoint);
    }

    // For outgoing packets, queue them for transmission
    if (packet.outgoing && result.success) {
      if (!tx_queue_->try_push(std::move(result))) {
        ++stats_.queue_full_drops;
        if (config_.enable_tracing) {
          LOG_WARN("TX queue full, dropping processed packet");
        }
      }

      // Update max queue size
      auto size = tx_queue_->size_approx();
      auto max = stats_.tx_queue_max_size.load();
      while (size > max && !stats_.tx_queue_max_size.compare_exchange_weak(max, size)) {
        // Retry on failure
      }
    }
  }

  LOG_DEBUG("Process thread exiting");
}

void PipelineProcessor::tx_thread_loop() {
  LOG_DEBUG("TX thread started");

  while (tx_worker_->is_running()) {
    // Try to get a processed packet from the TX queue
    auto maybe_packet = tx_queue_->try_pop();

    if (!maybe_packet) {
      // Queue empty - brief sleep to avoid busy-waiting
      if (tx_queue_->size_approx() < config_.busy_wait_threshold) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
      continue;
    }

    auto& packet = *maybe_packet;
    auto start_time = Clock::now();

    // Send all encrypted packets
    std::size_t total_bytes = 0;
    for (const auto& encrypted_data : packet.packets) {
      if (socket_ != nullptr) {
        std::error_code ec;
        if (socket_->send(encrypted_data, packet.endpoint, ec)) {
          total_bytes += encrypted_data.size();
          ++stats_.tx_packets;
        } else {
          if (on_error_) {
            on_error_(packet.session_id, "Send failed: " + ec.message());
          }
        }
      }
    }

    auto tx_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - start_time);
    stats_.total_tx_time_ns += static_cast<std::uint64_t>(tx_time.count());
    stats_.tx_bytes += total_bytes;

    // Invoke completion callback
    if (on_tx_complete_ && total_bytes > 0) {
      on_tx_complete_(packet.session_id, total_bytes);
    }
  }

  LOG_DEBUG("TX thread exiting");
}

void PipelineProcessor::update_queue_stats() {
  auto rx_size = rx_queue_->size_approx();
  auto tx_size = tx_queue_->size_approx();

  auto rx_max = stats_.rx_queue_max_size.load();
  while (rx_size > rx_max && !stats_.rx_queue_max_size.compare_exchange_weak(rx_max, rx_size)) {
  }

  auto tx_max = stats_.tx_queue_max_size.load();
  while (tx_size > tx_max && !stats_.tx_queue_max_size.compare_exchange_weak(tx_max, tx_size)) {
  }
}

}  // namespace veil::transport
