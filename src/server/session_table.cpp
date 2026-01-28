#include "server/session_table.h"

#include <algorithm>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include <sstream>

#include "common/logging/logger.h"

namespace veil::server {

SessionTable::SessionTable(std::size_t max_clients, std::chrono::seconds session_timeout,
                           const std::string& ip_pool_start, const std::string& ip_pool_end,
                           std::function<TimePoint()> now_fn)
    : max_clients_(max_clients),
      session_timeout_(session_timeout),
      now_fn_(std::move(now_fn)),
      ip_pool_start_(ip_to_uint(ip_pool_start)),
      ip_pool_end_(ip_to_uint(ip_pool_end)) {
  // Initialize IP pool.
  for (std::uint32_t ip = ip_pool_start_; ip <= ip_pool_end_; ++ip) {
    available_ips_.push_back(ip);
  }
  LOG_INFO("Session table initialized with {} available IPs", available_ips_.size());
}

std::uint32_t SessionTable::ip_to_uint(const std::string& ip) {
  struct in_addr addr {};
  inet_pton(AF_INET, ip.c_str(), &addr);
  return ntohl(addr.s_addr);
}

std::string SessionTable::uint_to_ip(std::uint32_t ip) {
  struct in_addr addr {};
  addr.s_addr = htonl(ip);
  char buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &addr, buf, sizeof(buf));
  return buf;
}

std::optional<std::string> SessionTable::allocate_ip() {
  if (available_ips_.empty()) {
    return std::nullopt;
  }

  std::uint32_t ip = available_ips_.back();
  available_ips_.pop_back();
  return uint_to_ip(ip);
}

void SessionTable::release_ip(const std::string& ip) {
  std::uint32_t ip_uint = ip_to_uint(ip);
  if (ip_uint >= ip_pool_start_ && ip_uint <= ip_pool_end_) {
    available_ips_.push_back(ip_uint);
  }
}

std::uint64_t SessionTable::generate_session_id() { return next_session_id_++; }

std::optional<std::uint64_t> SessionTable::create_session(
    const transport::UdpEndpoint& endpoint, std::unique_ptr<transport::TransportSession> transport) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (sessions_.size() >= max_clients_) {
    stats_.sessions_rejected_full++;
    LOG_WARN("Session table full, rejecting client {}:{}", endpoint.host, endpoint.port);
    return std::nullopt;
  }

  // Allocate IP.
  auto ip = allocate_ip();
  if (!ip) {
    stats_.sessions_rejected_full++;
    LOG_WARN("No IPs available, rejecting client {}:{}", endpoint.host, endpoint.port);
    return std::nullopt;
  }

  // Create session.
  auto session = std::make_unique<ClientSession>();
  session->session_id = generate_session_id();
  session->endpoint = endpoint;
  session->tunnel_ip = *ip;
  session->transport = std::move(transport);
  session->connected_at = now_fn_();
  session->last_activity = session->connected_at;

  // Update indices.
  std::string endpoint_key = endpoint.host + ":" + std::to_string(endpoint.port);
  endpoint_index_[endpoint_key] = session->session_id;
  ip_index_[*ip] = session->session_id;

  std::uint64_t id = session->session_id;
  sessions_[id] = std::move(session);

  stats_.active_sessions = sessions_.size();
  stats_.total_sessions_created++;

  LOG_INFO("Created session {} for {}:{} with tunnel IP {}", id, endpoint.host, endpoint.port, *ip);
  return id;
}

ClientSession* SessionTable::find_by_id(std::uint64_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    return it->second.get();
  }
  return nullptr;
}

ClientSession* SessionTable::find_by_endpoint(const transport::UdpEndpoint& endpoint) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string key = endpoint.host + ":" + std::to_string(endpoint.port);
  auto it = endpoint_index_.find(key);
  if (it != endpoint_index_.end()) {
    auto session_it = sessions_.find(it->second);
    if (session_it != sessions_.end()) {
      return session_it->second.get();
    }
  }
  return nullptr;
}

ClientSession* SessionTable::find_by_tunnel_ip(const std::string& ip) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = ip_index_.find(ip);
  if (it != ip_index_.end()) {
    auto session_it = sessions_.find(it->second);
    if (session_it != sessions_.end()) {
      return session_it->second.get();
    }
  }
  return nullptr;
}

void SessionTable::update_activity(std::uint64_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    it->second->last_activity = now_fn_();
  }
}

bool SessionTable::update_tunnel_ip(std::uint64_t session_id, const std::string& new_ip) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return false;
  }

  const std::string& old_ip = it->second->tunnel_ip;

  // Skip if IP hasn't changed
  if (old_ip == new_ip) {
    return true;
  }

  // Update the IP index: remove old mapping, add new one
  ip_index_.erase(old_ip);
  ip_index_[new_ip] = session_id;

  // Update the session's tunnel IP
  it->second->tunnel_ip = new_ip;

  LOG_INFO("Updated tunnel IP for session {} from {} to {} (client uses own IP)",
           session_id, old_ip, new_ip);

  return true;
}

bool SessionTable::remove_session(std::uint64_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return false;
  }

  // Remove from indices.
  std::string endpoint_key =
      it->second->endpoint.host + ":" + std::to_string(it->second->endpoint.port);
  endpoint_index_.erase(endpoint_key);
  ip_index_.erase(it->second->tunnel_ip);

  // Release IP.
  release_ip(it->second->tunnel_ip);

  LOG_INFO("Removed session {} ({}:{}, IP {})", session_id, it->second->endpoint.host,
           it->second->endpoint.port, it->second->tunnel_ip);

  sessions_.erase(it);
  stats_.active_sessions = sessions_.size();

  return true;
}

std::size_t SessionTable::cleanup_expired() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto now = now_fn_();
  std::vector<std::uint64_t> expired;

  for (const auto& [id, session] : sessions_) {
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - session->last_activity);
    if (age >= session_timeout_) {
      expired.push_back(id);
    }
  }

  for (std::uint64_t id : expired) {
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
      std::string endpoint_key =
          it->second->endpoint.host + ":" + std::to_string(it->second->endpoint.port);
      endpoint_index_.erase(endpoint_key);
      ip_index_.erase(it->second->tunnel_ip);
      release_ip(it->second->tunnel_ip);

      LOG_INFO("Session {} timed out", id);
      sessions_.erase(it);
      stats_.sessions_timed_out++;
    }
  }

  stats_.active_sessions = sessions_.size();
  return expired.size();
}

std::vector<SessionSnapshot> SessionTable::get_all_sessions() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SessionSnapshot> result;
  result.reserve(sessions_.size());
  for (const auto& [id, session] : sessions_) {
    SessionSnapshot snapshot;
    snapshot.session_id = session->session_id;
    snapshot.endpoint = session->endpoint;
    snapshot.tunnel_ip = session->tunnel_ip;
    snapshot.connected_at = session->connected_at;
    snapshot.last_activity = session->last_activity;
    snapshot.bytes_received = session->bytes_received;
    snapshot.bytes_sent = session->bytes_sent;
    snapshot.packets_received = session->packets_received;
    snapshot.packets_sent = session->packets_sent;
    result.push_back(std::move(snapshot));
  }
  return result;
}

}  // namespace veil::server
