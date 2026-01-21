#include "update_checker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QNetworkRequest>
#include <QRegularExpression>

#include "common/version.h"

namespace veil::gui {

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent),
      networkManager_(std::make_unique<QNetworkAccessManager>(this)) {
  connect(networkManager_.get(), &QNetworkAccessManager::finished,
          this, &UpdateChecker::onNetworkReply);
}

UpdateChecker::~UpdateChecker() = default;

QString UpdateChecker::currentVersion() {
  return QString::fromUtf8(veil::kVersionString);
}

void UpdateChecker::checkForUpdates() {
  if (checkInProgress_) {
    return;
  }

  checkInProgress_ = true;

  QNetworkRequest request;
  request.setUrl(QUrl(QString::fromUtf8(veil::kGitHubReleasesApi)));
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QString("VEIL-VPN-Client/%1").arg(currentVersion()));
  request.setRawHeader("Accept", "application/vnd.github.v3+json");

  networkManager_->get(request);
}

void UpdateChecker::onNetworkReply(QNetworkReply* reply) {
  checkInProgress_ = false;
  reply->deleteLater();

  if (reply->error() != QNetworkReply::NoError) {
    emit checkFailed(reply->errorString());
    return;
  }

  QByteArray data = reply->readAll();
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    emit checkFailed(QString("Failed to parse response: %1").arg(parseError.errorString()));
    return;
  }

  if (!doc.isObject()) {
    emit checkFailed("Invalid response format");
    return;
  }

  QJsonObject release = doc.object();

  // Extract version from tag_name (e.g., "v1.1.0" -> "1.1.0")
  QString tagName = release["tag_name"].toString();
  QString latestVersion = tagName;
  if (latestVersion.startsWith('v') || latestVersion.startsWith('V')) {
    latestVersion = latestVersion.mid(1);
  }

  // Compare versions
  int comparison = compareVersions(latestVersion, currentVersion());

  if (comparison <= 0) {
    emit noUpdateAvailable();
    return;
  }

  // Build update info
  UpdateInfo info;
  info.version = latestVersion;
  info.releaseUrl = release["html_url"].toString();
  info.releaseNotes = release["body"].toString();
  info.publishedAt = release["published_at"].toString();
  info.isPrerelease = release["prerelease"].toBool();

  // Find the Windows installer in assets
  QJsonArray assets = release["assets"].toArray();
  for (const auto& assetVal : assets) {
    QJsonObject asset = assetVal.toObject();
    QString name = asset["name"].toString();
    if (name.endsWith("-setup.exe") || (name.contains("windows") && name.endsWith(".exe"))) {
      info.downloadUrl = asset["browser_download_url"].toString();
      break;
    }
  }

  // If no Windows installer found, use the release page URL
  if (info.downloadUrl.isEmpty()) {
    info.downloadUrl = info.releaseUrl;
  }

  emit updateAvailable(info);
}

int UpdateChecker::compareVersions(const QString& v1, const QString& v2) {
  // Parse version strings like "1.0.0", "1.2.3-beta", etc.
  static QRegularExpression versionRegex(R"((\d+)(?:\.(\d+))?(?:\.(\d+))?(?:-(.+))?)");

  auto match1 = versionRegex.match(v1);
  auto match2 = versionRegex.match(v2);

  if (!match1.hasMatch() || !match2.hasMatch()) {
    // Fallback to string comparison
    return v1.compare(v2);
  }

  // Compare major, minor, patch
  for (int i = 1; i <= 3; ++i) {
    int n1 = match1.captured(i).isEmpty() ? 0 : match1.captured(i).toInt();
    int n2 = match2.captured(i).isEmpty() ? 0 : match2.captured(i).toInt();
    if (n1 > n2) return 1;
    if (n1 < n2) return -1;
  }

  // Versions are equal in numbers; check prerelease suffix
  QString suffix1 = match1.captured(4);
  QString suffix2 = match2.captured(4);

  // No suffix is considered "greater" than having a suffix (1.0.0 > 1.0.0-beta)
  if (suffix1.isEmpty() && !suffix2.isEmpty()) return 1;
  if (!suffix1.isEmpty() && suffix2.isEmpty()) return -1;
  if (!suffix1.isEmpty() && !suffix2.isEmpty()) {
    return suffix1.compare(suffix2);
  }

  return 0;  // Equal
}

}  // namespace veil::gui
