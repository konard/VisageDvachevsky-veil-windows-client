#include "common/auth/client_registry.h"

#include <sodium.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <utility>

namespace veil::auth {

bool is_valid_client_id(const std::string& client_id) {
  if (client_id.empty() || client_id.size() > kMaxClientIdLength) {
    return false;
  }
  return std::all_of(client_id.begin(), client_id.end(), [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
  });
}

bool is_valid_psk_size(std::size_t size) {
  return size >= kMinPskSize && size <= kMaxPskSize;
}

ClientRegistry::~ClientRegistry() {
  // SECURITY: Clear all PSKs on destruction
  std::unique_lock lock(mutex_);
  for (auto& [id, entry] : clients_) {
    if (!entry.psk.empty()) {
      sodium_memzero(entry.psk.data(), entry.psk.size());
    }
  }
  clients_.clear();
  if (fallback_psk_.has_value() && !fallback_psk_->empty()) {
    sodium_memzero(fallback_psk_->data(), fallback_psk_->size());
  }
  fallback_psk_.reset();
}

ClientRegistry::ClientRegistry(ClientRegistry&& other) noexcept {
  std::unique_lock other_lock(other.mutex_);
  clients_ = std::move(other.clients_);
  fallback_psk_ = std::move(other.fallback_psk_);
}

ClientRegistry& ClientRegistry::operator=(ClientRegistry&& other) noexcept {
  if (this != &other) {
    // Lock both mutexes in a consistent order to avoid deadlock
    std::unique_lock lock1(mutex_, std::defer_lock);
    std::unique_lock lock2(other.mutex_, std::defer_lock);
    std::lock(lock1, lock2);

    // Clear our existing data securely
    for (auto& [id, entry] : clients_) {
      if (!entry.psk.empty()) {
        sodium_memzero(entry.psk.data(), entry.psk.size());
      }
    }
    if (fallback_psk_.has_value() && !fallback_psk_->empty()) {
      sodium_memzero(fallback_psk_->data(), fallback_psk_->size());
    }

    // Move from other
    clients_ = std::move(other.clients_);
    fallback_psk_ = std::move(other.fallback_psk_);
  }
  return *this;
}

bool ClientRegistry::set_fallback_psk(std::vector<std::uint8_t> psk) {
  if (!is_valid_psk_size(psk.size())) {
    return false;
  }
  std::unique_lock lock(mutex_);
  // Clear old fallback PSK if exists
  if (fallback_psk_.has_value() && !fallback_psk_->empty()) {
    sodium_memzero(fallback_psk_->data(), fallback_psk_->size());
  }
  fallback_psk_ = std::move(psk);
  return true;
}

void ClientRegistry::clear_fallback_psk() {
  std::unique_lock lock(mutex_);
  if (fallback_psk_.has_value() && !fallback_psk_->empty()) {
    sodium_memzero(fallback_psk_->data(), fallback_psk_->size());
  }
  fallback_psk_.reset();
}

bool ClientRegistry::has_fallback_psk() const {
  std::shared_lock lock(mutex_);
  return fallback_psk_.has_value();
}

bool ClientRegistry::add_client(const std::string& client_id, std::vector<std::uint8_t> psk) {
  if (!is_valid_client_id(client_id)) {
    return false;
  }
  if (!is_valid_psk_size(psk.size())) {
    return false;
  }
  std::unique_lock lock(mutex_);
  if (clients_.contains(client_id)) {
    return false;  // Client already exists
  }
  clients_[client_id] = ClientEntry{.psk = std::move(psk), .enabled = true};
  return true;
}

bool ClientRegistry::remove_client(const std::string& client_id) {
  std::unique_lock lock(mutex_);
  auto it = clients_.find(client_id);
  if (it == clients_.end()) {
    return false;
  }
  // SECURITY: Clear PSK before removal
  if (!it->second.psk.empty()) {
    sodium_memzero(it->second.psk.data(), it->second.psk.size());
  }
  clients_.erase(it);
  return true;
}

std::optional<std::vector<std::uint8_t>> ClientRegistry::get_psk(const std::string& client_id) const {
  std::shared_lock lock(mutex_);
  auto it = clients_.find(client_id);
  if (it == clients_.end() || !it->second.enabled) {
    return std::nullopt;
  }
  return it->second.psk;
}

std::optional<std::vector<std::uint8_t>> ClientRegistry::get_psk_or_fallback(
    const std::string& client_id) const {
  std::shared_lock lock(mutex_);

  // If client_id is provided, try to find it
  if (!client_id.empty()) {
    auto it = clients_.find(client_id);
    if (it != clients_.end() && it->second.enabled) {
      return it->second.psk;
    }
  }

  // Fall back to fallback PSK
  return fallback_psk_;
}

std::optional<std::vector<std::uint8_t>> ClientRegistry::get_fallback_psk() const {
  std::shared_lock lock(mutex_);
  return fallback_psk_;
}

bool ClientRegistry::enable_client(const std::string& client_id) {
  std::unique_lock lock(mutex_);
  auto it = clients_.find(client_id);
  if (it == clients_.end()) {
    return false;
  }
  it->second.enabled = true;
  return true;
}

bool ClientRegistry::disable_client(const std::string& client_id) {
  std::unique_lock lock(mutex_);
  auto it = clients_.find(client_id);
  if (it == clients_.end()) {
    return false;
  }
  it->second.enabled = false;
  return true;
}

bool ClientRegistry::has_client(const std::string& client_id) const {
  std::shared_lock lock(mutex_);
  return clients_.contains(client_id);
}

bool ClientRegistry::is_client_enabled(const std::string& client_id) const {
  std::shared_lock lock(mutex_);
  auto it = clients_.find(client_id);
  return it != clients_.end() && it->second.enabled;
}

std::size_t ClientRegistry::client_count() const {
  std::shared_lock lock(mutex_);
  return clients_.size();
}

std::vector<std::string> ClientRegistry::get_client_ids() const {
  std::shared_lock lock(mutex_);
  std::vector<std::string> ids;
  ids.reserve(clients_.size());
  for (const auto& [id, entry] : clients_) {
    ids.push_back(id);
  }
  return ids;
}

std::vector<std::pair<std::string, std::vector<std::uint8_t>>> ClientRegistry::get_all_enabled_psks()
    const {
  std::shared_lock lock(mutex_);
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> result;
  result.reserve(clients_.size());
  for (const auto& [id, entry] : clients_) {
    if (entry.enabled) {
      result.emplace_back(id, entry.psk);
    }
  }
  return result;
}

}  // namespace veil::auth
