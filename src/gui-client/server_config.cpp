#include "server_config.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace veil::gui {

QJsonObject ServerConfig::toJson() const {
  QJsonObject json;
  json["id"] = id;
  json["name"] = name;
  json["address"] = address;
  json["port"] = static_cast<int>(port);
  json["keyFilePath"] = keyFilePath;
  json["obfuscationSeedPath"] = obfuscationSeedPath;
  json["isFavorite"] = isFavorite;
  json["lastLatencyMs"] = lastLatencyMs;
  json["lastConnected"] = lastConnected.toString(Qt::ISODate);
  json["dateAdded"] = dateAdded.toString(Qt::ISODate);
  json["notes"] = notes;
  return json;
}

ServerConfig ServerConfig::fromJson(const QJsonObject& json) {
  ServerConfig config;
  config.id = json["id"].toString();
  config.name = json["name"].toString();
  config.address = json["address"].toString();
  config.port = static_cast<uint16_t>(json["port"].toInt(4433));
  config.keyFilePath = json["keyFilePath"].toString();
  config.obfuscationSeedPath = json["obfuscationSeedPath"].toString();
  config.isFavorite = json["isFavorite"].toBool(false);
  config.lastLatencyMs = json["lastLatencyMs"].toInt(-1);
  config.lastConnected = QDateTime::fromString(json["lastConnected"].toString(), Qt::ISODate);
  config.dateAdded = QDateTime::fromString(json["dateAdded"].toString(), Qt::ISODate);
  config.notes = json["notes"].toString();
  return config;
}

bool ServerConfig::isValid() const {
  return !id.isEmpty() && !address.isEmpty() && port != 0;
}

bool ServerConfig::hasCustomCrypto() const {
  return !keyFilePath.isEmpty() || !obfuscationSeedPath.isEmpty();
}

ServerListManager::ServerListManager() {
  loadServers();
}

void ServerListManager::loadServers() {
  QSettings settings("VEIL", "VPN Client");

  servers_.clear();

  int size = settings.beginReadArray("servers");
  for (int i = 0; i < size; ++i) {
    settings.setArrayIndex(i);
    QJsonObject json = QJsonDocument::fromJson(
        settings.value("data").toString().toUtf8()).object();
    servers_.append(ServerConfig::fromJson(json));
  }
  settings.endArray();

  currentServerId_ = settings.value("currentServerId", "").toString();

  // If no servers exist, check for legacy single server config and migrate it
  if (servers_.isEmpty()) {
    QString legacyAddress = settings.value("server/address", "").toString();
    if (!legacyAddress.isEmpty()) {
      ServerConfig legacy;
      legacy.id = generateServerId();
      legacy.name = "Default Server";
      legacy.address = legacyAddress;
      legacy.port = static_cast<uint16_t>(settings.value("server/port", 4433).toInt());
      legacy.dateAdded = QDateTime::currentDateTime();
      legacy.isFavorite = true;
      servers_.append(legacy);
      currentServerId_ = legacy.id;
      saveServers();
    }
  }
}

void ServerListManager::saveServers() {
  QSettings settings("VEIL", "VPN Client");

  settings.beginWriteArray("servers", static_cast<int>(servers_.size()));
  for (int i = 0; i < servers_.size(); ++i) {
    settings.setArrayIndex(i);
    QJsonDocument doc(servers_[i].toJson());
    settings.setValue("data", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
  }
  settings.endArray();

  settings.setValue("currentServerId", currentServerId_);
  settings.sync();
}

void ServerListManager::addServer(const ServerConfig& server) {
  servers_.append(server);
  saveServers();
}

bool ServerListManager::updateServer(const QString& id, const ServerConfig& server) {
  int index = findServerIndex(id);
  if (index >= 0) {
    servers_[index] = server;
    saveServers();
    return true;
  }
  return false;
}

bool ServerListManager::removeServer(const QString& id) {
  int index = findServerIndex(id);
  if (index >= 0) {
    servers_.removeAt(index);
    if (currentServerId_ == id) {
      currentServerId_ = servers_.isEmpty() ? "" : servers_.first().id;
    }
    saveServers();
    return true;
  }
  return false;
}

std::optional<ServerConfig> ServerListManager::getServer(const QString& id) const {
  int index = findServerIndex(id);
  if (index >= 0) {
    return servers_[index];
  }
  return std::nullopt;
}

QList<ServerConfig> ServerListManager::getAllServers() const {
  return servers_;
}

QList<ServerConfig> ServerListManager::getFavoriteServers() const {
  QList<ServerConfig> favorites;
  for (const auto& server : servers_) {
    if (server.isFavorite) {
      favorites.append(server);
    }
  }
  return favorites;
}

QList<ServerConfig> ServerListManager::getServersSortedByLatency() const {
  QList<ServerConfig> sorted = servers_;

  std::sort(sorted.begin(), sorted.end(), [](const ServerConfig& a, const ServerConfig& b) {
    // Favorites first
    if (a.isFavorite != b.isFavorite) {
      return a.isFavorite;
    }

    // Then by latency (valid latencies first, sorted ascending)
    bool aHasLatency = a.lastLatencyMs >= 0;
    bool bHasLatency = b.lastLatencyMs >= 0;

    if (aHasLatency && bHasLatency) {
      return a.lastLatencyMs < b.lastLatencyMs;
    }
    if (aHasLatency != bHasLatency) {
      return aHasLatency;
    }

    // Finally by name
    return a.name < b.name;
  });

  return sorted;
}

bool ServerListManager::toggleFavorite(const QString& id) {
  int index = findServerIndex(id);
  if (index >= 0) {
    servers_[index].isFavorite = !servers_[index].isFavorite;
    saveServers();
    return true;
  }
  return false;
}

bool ServerListManager::updateLatency(const QString& id, int latencyMs) {
  int index = findServerIndex(id);
  if (index >= 0) {
    servers_[index].lastLatencyMs = latencyMs;
    saveServers();
    return true;
  }
  return false;
}

bool ServerListManager::markAsConnected(const QString& id) {
  int index = findServerIndex(id);
  if (index >= 0) {
    servers_[index].lastConnected = QDateTime::currentDateTime();
    saveServers();
    return true;
  }
  return false;
}

QString ServerListManager::getCurrentServerId() const {
  return currentServerId_;
}

void ServerListManager::setCurrentServerId(const QString& id) {
  currentServerId_ = id;
  saveServers();
}

std::optional<ServerConfig> ServerListManager::importFromUri(const QString& uri, QString* error) {
  // Parse VEIL connection URI: veil://host:port?name=xxx&key=xxx
  QUrl url(uri);

  if (url.scheme() != "veil") {
    if (error != nullptr) *error = "Invalid URI scheme. Expected 'veil://'";
    return std::nullopt;
  }

  ServerConfig config;
  config.id = generateServerId();
  config.address = url.host();
  config.port = static_cast<uint16_t>(url.port(4433));
  config.dateAdded = QDateTime::currentDateTime();

  QUrlQuery query(url);
  config.name = query.queryItemValue("name");
  if (config.name.isEmpty()) {
    config.name = QString("%1:%2").arg(config.address).arg(config.port);
  }

  config.keyFilePath = query.queryItemValue("key");
  config.obfuscationSeedPath = query.queryItemValue("seed");
  config.notes = query.queryItemValue("notes");

  if (!config.isValid()) {
    if (error != nullptr) *error = "Invalid server configuration in URI";
    return std::nullopt;
  }

  return config;
}

std::optional<ServerConfig> ServerListManager::importFromJsonFile(const QString& filePath, QString* error) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) *error = "Failed to open file: " + file.errorString();
    return std::nullopt;
  }

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError) {
    if (error != nullptr) *error = "JSON parse error: " + parseError.errorString();
    return std::nullopt;
  }

  if (!doc.isObject()) {
    if (error != nullptr) *error = "Invalid JSON format: expected object";
    return std::nullopt;
  }

  ServerConfig config = ServerConfig::fromJson(doc.object());

  // Ensure fresh ID and timestamp
  if (config.id.isEmpty()) {
    config.id = generateServerId();
  }
  config.dateAdded = QDateTime::currentDateTime();

  if (!config.isValid()) {
    if (error != nullptr) *error = "Invalid server configuration in JSON";
    return std::nullopt;
  }

  return config;
}

QString ServerListManager::exportServerToJson(const QString& id) const {
  auto server = getServer(id);
  if (!server) {
    return "";
  }

  QJsonDocument doc(server->toJson());
  return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

QString ServerListManager::generateServerId() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int ServerListManager::findServerIndex(const QString& id) const {
  for (int i = 0; i < servers_.size(); ++i) {
    if (servers_[i].id == id) {
      return i;
    }
  }
  return -1;
}

}  // namespace veil::gui
