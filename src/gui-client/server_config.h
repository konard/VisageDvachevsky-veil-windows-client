#pragma once

#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <optional>

namespace veil::gui {

/// Server configuration entry for multi-server management
struct ServerConfig {
  QString id;                      ///< Unique server identifier (UUID)
  QString name;                    ///< User-friendly server name
  QString address;                 ///< Server hostname or IP address
  uint16_t port{4433};            ///< Server port
  QString keyFilePath;             ///< Path to key file (optional, can inherit from global)
  QString obfuscationSeedPath;     ///< Path to obfuscation seed (optional)
  bool isFavorite{false};         ///< Favorite flag for quick access
  int lastLatencyMs{-1};          ///< Last measured latency (-1 = not measured)
  QDateTime lastConnected;         ///< Last successful connection timestamp
  QDateTime dateAdded;             ///< When server was added
  QString notes;                   ///< User notes about the server

  /// Convert to JSON for storage
  QJsonObject toJson() const;

  /// Create from JSON
  static ServerConfig fromJson(const QJsonObject& json);

  /// Validate server configuration
  bool isValid() const;

  /// Check if server has custom crypto config (vs global defaults)
  bool hasCustomCrypto() const;
};

/// Server list manager for storing and retrieving server configurations
class ServerListManager {
 public:
  ServerListManager();

  /// Load servers from QSettings
  void loadServers();

  /// Save servers to QSettings
  void saveServers();

  /// Add a new server
  void addServer(const ServerConfig& server);

  /// Update existing server
  bool updateServer(const QString& id, const ServerConfig& server);

  /// Remove server by ID
  bool removeServer(const QString& id);

  /// Get server by ID
  std::optional<ServerConfig> getServer(const QString& id) const;

  /// Get all servers
  QList<ServerConfig> getAllServers() const;

  /// Get favorite servers only
  QList<ServerConfig> getFavoriteServers() const;

  /// Get servers sorted by latency (favorites first, then by latency)
  QList<ServerConfig> getServersSortedByLatency() const;

  /// Toggle favorite status
  bool toggleFavorite(const QString& id);

  /// Update server latency
  bool updateLatency(const QString& id, int latencyMs);

  /// Mark server as recently connected
  bool markAsConnected(const QString& id);

  /// Get currently selected server ID
  QString getCurrentServerId() const;

  /// Set currently selected server
  void setCurrentServerId(const QString& id);

  /// Import server from VEIL connection URI (veil://host:port?params)
  std::optional<ServerConfig> importFromUri(const QString& uri, QString* error = nullptr);

  /// Import server from JSON config file
  std::optional<ServerConfig> importFromJsonFile(const QString& filePath, QString* error = nullptr);

  /// Export server to JSON string
  QString exportServerToJson(const QString& id) const;

  /// Generate unique server ID
  static QString generateServerId();

 private:
  QList<ServerConfig> servers_;
  QString currentServerId_;

  /// Find server index by ID
  int findServerIndex(const QString& id) const;
};

}  // namespace veil::gui
