#include "transport/event_loop/threaded_event_loop.h"

#include <chrono>

#include "common/logging/logger.h"
#include "transport/session/transport_session.h"

namespace veil::transport {

ThreadedEventLoop::ThreadedEventLoop(ThreadedEventLoopConfig config)
    : config_(std::move(config)),
      event_loop_(std::make_unique<EventLoop>(config_.event_loop_config)),
      last_perf_log_(Clock::now()) {
  LOG_INFO("ThreadedEventLoop created with mode={}",
           config_.threading_mode == ThreadingMode::kPipeline ? "Pipeline" : "SingleThreaded");
}

ThreadedEventLoop::~ThreadedEventLoop() {
  stop();
  cleanup_pipeline_mode();
  LOG_DEBUG("ThreadedEventLoop destroyed");
}

bool ThreadedEventLoop::add_session(TransportSession* session,
                                     UdpSocket* socket,
                                     const UdpEndpoint& remote,
                                     PacketHandler on_packet,
                                     ErrorHandler on_error) {
  if (session == nullptr || socket == nullptr) {
    LOG_ERROR("ThreadedEventLoop::add_session: null session or socket");
    return false;
  }

  const SessionId session_id = session->session_id();

  if (config_.threading_mode == ThreadingMode::kPipeline) {
    // Pipeline mode: create a pipeline processor for this session
    PipelineSessionInfo info;
    info.session = session;
    info.socket = socket;
    info.remote = remote;
    info.on_packet = std::move(on_packet);
    info.on_error = std::move(on_error);

    // Create pipeline processor
    info.pipeline = std::make_unique<PipelineProcessor>(session, config_.pipeline_config);
    info.pipeline->set_socket(socket);

    pipeline_sessions_[session_id] = std::move(info);

    LOG_DEBUG("Added session {} in pipeline mode", session_id);
    return true;
  }

  // Single-threaded mode: use the base event loop directly
  // We need to wrap the packet handler to decrypt packets
  auto wrapped_handler = [session, on_packet = std::move(on_packet)](
                             SessionId sid,
                             std::span<const std::uint8_t> data,
                             const UdpEndpoint& source) {
    // Decrypt the packet
    auto decrypted = session->decrypt_packet(data);
    if (decrypted && on_packet) {
      // For now, pass raw decrypted data - the caller handles frame processing
      // In a real implementation, this would need to serialize the frames
      for (const auto& frame : *decrypted) {
        if (frame.kind == mux::FrameKind::kData) {
          on_packet(sid, frame.data.payload, source);
        }
      }
    }
  };

  return event_loop_->add_socket(socket, session_id, remote, std::move(wrapped_handler),
                                  {}, {}, {}, std::move(on_error));
}

bool ThreadedEventLoop::remove_session(SessionId session_id) {
  if (config_.threading_mode == ThreadingMode::kPipeline) {
    auto it = pipeline_sessions_.find(session_id);
    if (it == pipeline_sessions_.end()) {
      return false;
    }

    // Stop the pipeline if running
    if (it->second.pipeline) {
      it->second.pipeline->stop();
    }

    pipeline_sessions_.erase(it);
    LOG_DEBUG("Removed session {} from pipeline mode", session_id);
    return true;
  }

  // Single-threaded mode: find and remove socket
  // Note: This is a simplified implementation; the actual socket fd would need to be tracked
  LOG_WARN("remove_session in single-threaded mode not fully implemented");
  return false;
}

bool ThreadedEventLoop::send_data(SessionId session_id,
                                   std::span<const std::uint8_t> data,
                                   std::uint64_t stream_id) {
  if (config_.threading_mode == ThreadingMode::kPipeline) {
    auto it = pipeline_sessions_.find(session_id);
    if (it == pipeline_sessions_.end()) {
      LOG_ERROR("send_data: session {} not found", session_id);
      return false;
    }

    // Submit to pipeline for encryption and transmission
    return it->second.pipeline->submit_tx(session_id, data, it->second.remote, stream_id);
  }

  // Single-threaded mode: encrypt and send directly
  auto it = pipeline_sessions_.find(session_id);
  if (it == pipeline_sessions_.end()) {
    LOG_ERROR("send_data: session {} not found", session_id);
    return false;
  }

  // Encrypt the data
  auto encrypted = it->second.session->encrypt_data(data, stream_id);

  // Send each encrypted packet
  for (const auto& packet : encrypted) {
    std::error_code ec;
    if (!it->second.socket->send(packet, it->second.remote, ec)) {
      LOG_ERROR("send_data: send failed: {}", ec.message());
      return false;
    }
  }

  return true;
}

void ThreadedEventLoop::run() {
  if (running_.load()) {
    LOG_WARN("ThreadedEventLoop already running");
    return;
  }

  running_.store(true);
  last_perf_log_ = Clock::now();

  if (config_.threading_mode == ThreadingMode::kPipeline) {
    LOG_INFO("ThreadedEventLoop starting in pipeline mode");
    init_pipeline_mode();

    // Start all pipeline processors
    for (auto& [session_id, info] : pipeline_sessions_) {
      (void)session_id;  // Used in next loop iteration
      // Create callbacks that route to the session's handlers
      auto on_rx = [&info](std::uint64_t sid,
                            const std::vector<mux::MuxFrame>& frames,
                            const UdpEndpoint& source) {
        if (info.on_packet) {
          for (const auto& frame : frames) {
            if (frame.kind == mux::FrameKind::kData) {
              info.on_packet(sid, frame.data.payload, source);
            }
          }
        }
      };

      auto on_error = [&info](std::uint64_t sid, const std::string& error) {
        if (info.on_error) {
          info.on_error(sid, std::make_error_code(std::errc::io_error));
        }
        // NOLINTNEXTLINE(bugprone-lambda-function-name) - LOG_ERROR macro uses __FUNCTION__
        LOG_ERROR("Pipeline error for session {}: {}", sid, error);
      };

      info.pipeline->start(std::move(on_rx), {}, std::move(on_error));
    }

    // In pipeline mode, we still run the base event loop for socket I/O
    // The event loop handles the raw socket reads and feeds them to pipelines
    for (auto& [session_id, info] : pipeline_sessions_) {
      // Register socket for receiving
      auto packet_handler = [this, session_id](SessionId sid,
                                                std::span<const std::uint8_t> data,
                                                const UdpEndpoint& source) {
        auto it = pipeline_sessions_.find(session_id);
        if (it != pipeline_sessions_.end()) {
          it->second.pipeline->submit_rx(sid, data, source);
        }
      };

      event_loop_->add_socket(info.socket, session_id, info.remote,
                               std::move(packet_handler), {}, {}, {}, info.on_error);
    }

    // Run the event loop for I/O
    event_loop_->run();
  } else {
    // Single-threaded mode: just run the base event loop
    LOG_INFO("ThreadedEventLoop starting in single-threaded mode");
    event_loop_->run();
  }

  LOG_INFO("ThreadedEventLoop stopped");
}

void ThreadedEventLoop::stop() {
  if (!running_.load()) {
    return;
  }

  LOG_INFO("ThreadedEventLoop stopping...");
  running_.store(false);

  // Stop the base event loop
  event_loop_->stop();

  // Stop all pipeline processors
  if (config_.threading_mode == ThreadingMode::kPipeline) {
    for (auto& [session_id, info] : pipeline_sessions_) {
      if (info.pipeline) {
        info.pipeline->stop();
      }
    }
  }

  // Log final performance stats
  if (config_.enable_perf_logging) {
    log_performance();
  }
}

utils::TimerId ThreadedEventLoop::schedule_timer(std::chrono::steady_clock::duration after,
                                                   utils::TimerCallback callback) {
  return event_loop_->schedule_timer(after, std::move(callback));
}

bool ThreadedEventLoop::cancel_timer(utils::TimerId id) {
  return event_loop_->cancel_timer(id);
}

void ThreadedEventLoop::init_pipeline_mode() {
  LOG_DEBUG("Initializing pipeline mode");
  // Currently no additional initialization needed
  // Future: could pre-allocate buffers, pin threads to CPUs, etc.
}

void ThreadedEventLoop::cleanup_pipeline_mode() {
  LOG_DEBUG("Cleaning up pipeline mode");

  // Stop and destroy all pipeline processors
  for (auto& [session_id, info] : pipeline_sessions_) {
    if (info.pipeline) {
      info.pipeline->stop();
      info.pipeline.reset();
    }
  }
  pipeline_sessions_.clear();
}

void ThreadedEventLoop::log_performance() {
  if (config_.threading_mode != ThreadingMode::kPipeline) {
    return;
  }

  auto now = Clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_perf_log_);
  if (elapsed.count() == 0) {
    return;
  }

  // Aggregate stats from all pipelines
  std::uint64_t total_rx_packets = 0;
  std::uint64_t total_tx_packets = 0;
  std::uint64_t total_rx_bytes = 0;
  std::uint64_t total_tx_bytes = 0;
  std::uint64_t total_process_time_ns = 0;
  std::uint64_t total_processed = 0;

  for (const auto& [session_id, info] : pipeline_sessions_) {
    if (info.pipeline) {
      const auto& stats = info.pipeline->stats();
      total_rx_packets += stats.rx_packets.load();
      total_tx_packets += stats.tx_packets.load();
      total_rx_bytes += stats.rx_bytes.load();
      total_tx_bytes += stats.tx_bytes.load();
      total_process_time_ns += stats.total_process_time_ns.load();
      total_processed += stats.processed_packets.load();
    }
  }

  // Calculate rates
  const auto elapsed_sec = static_cast<std::uint64_t>(elapsed.count());
  auto rx_pps = (total_rx_packets - last_rx_packets_) / elapsed_sec;
  auto tx_pps = (total_tx_packets - last_tx_packets_) / elapsed_sec;
  auto rx_bps = (total_rx_bytes - last_rx_bytes_) * 8 / elapsed_sec;  // bits per second
  auto tx_bps = (total_tx_bytes - last_tx_bytes_) * 8 / elapsed_sec;

  // Calculate average latencies
  std::uint64_t avg_process_us = 0;
  if (total_processed > 0) {
    avg_process_us = (total_process_time_ns / total_processed) / 1000;  // ns to us
  }

  // Update metrics
  metrics_.rx_packets_per_sec.store(rx_pps);
  metrics_.tx_packets_per_sec.store(tx_pps);
  metrics_.rx_bytes_per_sec.store(rx_bps / 8);  // Back to bytes
  metrics_.tx_bytes_per_sec.store(tx_bps / 8);
  metrics_.avg_process_latency_us.store(avg_process_us);

  // Log performance
  LOG_INFO("Pipeline performance: RX={} pps ({:.2f} Mbps), TX={} pps ({:.2f} Mbps), "
           "avg_process_latency={}us",
           rx_pps, static_cast<double>(rx_bps) / 1e6,
           tx_pps, static_cast<double>(tx_bps) / 1e6,
           avg_process_us);

  // Update state for next iteration
  last_perf_log_ = now;
  last_rx_packets_ = total_rx_packets;
  last_tx_packets_ = total_tx_packets;
  last_rx_bytes_ = total_rx_bytes;
  last_tx_bytes_ = total_tx_bytes;
}

}  // namespace veil::transport
