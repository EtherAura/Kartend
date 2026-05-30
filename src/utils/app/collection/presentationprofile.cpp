#include "presentationprofile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "collection/generalsettings.h"
#include "pathutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace PresentationProfileIO {

namespace {

constexpr int kCurrentSchemaVersion = 1;

} // namespace

QJsonObject toJson(const PresentationProfile &p) {
  QJsonObject o;
  o["schemaVersion"] = p.schemaVersion <= 0 ? kCurrentSchemaVersion : p.schemaVersion;
  o["name"] = p.name;
  o["description"] = p.description;

  o["attractModeEnabled"] = p.attractModeEnabled;
  o["attractModeIdleTimeoutSec"] = p.attractModeIdleTimeoutSec;
  o["attractModeAutoScrollEnabled"] = p.attractModeAutoScrollEnabled;
  o["attractModeScrollSpeed"] = p.attractModeScrollSpeed;
  o["attractModeAdvanceSelectionEnabled"] = p.attractModeAdvanceSelectionEnabled;
  o["attractModeAdvanceSelectionIntervalSec"] = p.attractModeAdvanceSelectionIntervalSec;
  o["attractModeAdvanceSelectionRandom"] = p.attractModeAdvanceSelectionRandom;

  o["marqueeEnabled"] = p.marqueeEnabled;
  o["marqueeScreenName"] = p.marqueeScreenName;
  o["marqueeMode"] = p.marqueeMode;

  o["bootSplashEnabled"] = p.bootSplashEnabled;
  o["resumeFocusSplashEnabled"] = p.resumeFocusSplashEnabled;
  o["bootSplashTitle"] = p.bootSplashTitle;
  o["bootSplashSubtitle"] = p.bootSplashSubtitle;
  o["resumeFocusSplashTitle"] = p.resumeFocusSplashTitle;
  o["resumeFocusSplashSubtitle"] = p.resumeFocusSplashSubtitle;
  return o;
}

ErrorUtils::Result<PresentationProfile> fromJson(const QJsonObject &obj) {
  if (!obj.contains("schemaVersion")) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Presentation profile is missing the schemaVersion field",
                               "PresentationProfileIO::fromJson");
  }
  const int schema = obj.value("schemaVersion").toInt(-1);
  if (schema <= 0) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Presentation profile schemaVersion is invalid",
                               "PresentationProfileIO::fromJson")
        .withDetails(QStringLiteral("Value: %1").arg(schema));
  }
  if (schema > kCurrentSchemaVersion) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Presentation profile was written by a newer Kartend build",
                               "PresentationProfileIO::fromJson")
        .withDetails(QStringLiteral("Profile schema=%1, supported up to %2")
                         .arg(schema)
                         .arg(kCurrentSchemaVersion));
  }
  PresentationProfile p;
  p.schemaVersion = schema;
  p.name = obj.value("name").toString();
  p.description = obj.value("description").toString();
  p.attractModeEnabled = obj.value("attractModeEnabled").toBool(p.attractModeEnabled);
  p.attractModeIdleTimeoutSec =
      obj.value("attractModeIdleTimeoutSec").toInt(p.attractModeIdleTimeoutSec);
  p.attractModeAutoScrollEnabled =
      obj.value("attractModeAutoScrollEnabled").toBool(p.attractModeAutoScrollEnabled);
  p.attractModeScrollSpeed = obj.value("attractModeScrollSpeed").toDouble(p.attractModeScrollSpeed);
  p.attractModeAdvanceSelectionEnabled =
      obj.value("attractModeAdvanceSelectionEnabled").toBool(p.attractModeAdvanceSelectionEnabled);
  p.attractModeAdvanceSelectionIntervalSec = obj.value("attractModeAdvanceSelectionIntervalSec")
                                                 .toInt(p.attractModeAdvanceSelectionIntervalSec);
  p.attractModeAdvanceSelectionRandom =
      obj.value("attractModeAdvanceSelectionRandom").toBool(p.attractModeAdvanceSelectionRandom);
  p.marqueeEnabled = obj.value("marqueeEnabled").toBool(p.marqueeEnabled);
  p.marqueeScreenName = obj.value("marqueeScreenName").toString();
  p.marqueeMode = obj.value("marqueeMode").toInt(p.marqueeMode);
  p.bootSplashEnabled = obj.value("bootSplashEnabled").toBool(p.bootSplashEnabled);
  p.resumeFocusSplashEnabled =
      obj.value("resumeFocusSplashEnabled").toBool(p.resumeFocusSplashEnabled);
  p.bootSplashTitle = obj.value("bootSplashTitle").toString();
  p.bootSplashSubtitle = obj.value("bootSplashSubtitle").toString();
  p.resumeFocusSplashTitle = obj.value("resumeFocusSplashTitle").toString();
  p.resumeFocusSplashSubtitle = obj.value("resumeFocusSplashSubtitle").toString();
  return p;
}

ErrorUtils::Result<bool> exportToFile(const PresentationProfile &profile, const QString &filePath) {
  if (filePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Cannot export presentation profile to an empty path",
                               "PresentationProfileIO::exportToFile");
  }
  const QString parentDir = QFileInfo(filePath).absolutePath();
  if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to create parent directory for presentation profile",
                               "PresentationProfileIO::exportToFile");
  }
  QSaveFile f(filePath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to open presentation profile for writing",
                               "PresentationProfileIO::exportToFile")
        .withDetails(f.errorString());
  }
  const QByteArray bytes = QJsonDocument(toJson(profile)).toJson(QJsonDocument::Indented);
  if (f.write(bytes) != bytes.size()) {
    f.cancelWriting();
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Short write on presentation profile export",
                               "PresentationProfileIO::exportToFile");
  }
  if (!f.commit()) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to commit presentation profile write",
                               "PresentationProfileIO::exportToFile");
  }
  PathUtils::syncDirectory(parentDir);
  return true;
}

ErrorUtils::Result<PresentationProfile> importFromFile(const QString &filePath) {
  if (filePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Cannot import presentation profile from an empty path",
                               "PresentationProfileIO::importFromFile");
  }
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileReadError,
                               "Failed to open presentation profile for reading",
                               "PresentationProfileIO::importFromFile")
        .withDetails(f.errorString());
  }
  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Presentation profile is not valid JSON",
                               "PresentationProfileIO::importFromFile")
        .withDetails(err.errorString());
  }
  return fromJson(doc.object());
}

void applyTo(const PresentationProfile &p, GeneralSettings &t) {
  t.attract.attractModeEnabled = p.attractModeEnabled;
  t.attract.attractModeIdleTimeoutSec = p.attractModeIdleTimeoutSec;
  t.attract.attractModeAutoScrollEnabled = p.attractModeAutoScrollEnabled;
  t.attract.attractModeScrollSpeed = p.attractModeScrollSpeed;
  t.attract.attractModeAdvanceSelectionEnabled = p.attractModeAdvanceSelectionEnabled;
  t.attract.attractModeAdvanceSelectionIntervalSec = p.attractModeAdvanceSelectionIntervalSec;
  t.attract.attractModeAdvanceSelectionRandom = p.attractModeAdvanceSelectionRandom;
  t.marquee.marqueeEnabled = p.marqueeEnabled;
  t.marquee.marqueeScreenName = p.marqueeScreenName;
  t.marquee.marqueeMode = p.marqueeMode;
  t.splash.bootSplashEnabled = p.bootSplashEnabled;
  t.splash.resumeFocusSplashEnabled = p.resumeFocusSplashEnabled;
  t.splash.bootSplashTitle = p.bootSplashTitle;
  t.splash.bootSplashSubtitle = p.bootSplashSubtitle;
  t.splash.resumeFocusSplashTitle = p.resumeFocusSplashTitle;
  t.splash.resumeFocusSplashSubtitle = p.resumeFocusSplashSubtitle;
}

PresentationProfile fromGeneralSettings(const GeneralSettings &s, const QString &name) {
  PresentationProfile p;
  p.schemaVersion = kCurrentSchemaVersion;
  p.name = name.isEmpty() ? QStringLiteral("Current") : name;
  p.attractModeEnabled = s.attract.attractModeEnabled;
  p.attractModeIdleTimeoutSec = s.attract.attractModeIdleTimeoutSec;
  p.attractModeAutoScrollEnabled = s.attract.attractModeAutoScrollEnabled;
  p.attractModeScrollSpeed = s.attract.attractModeScrollSpeed;
  p.attractModeAdvanceSelectionEnabled = s.attract.attractModeAdvanceSelectionEnabled;
  p.attractModeAdvanceSelectionIntervalSec = s.attract.attractModeAdvanceSelectionIntervalSec;
  p.attractModeAdvanceSelectionRandom = s.attract.attractModeAdvanceSelectionRandom;
  p.marqueeEnabled = s.marquee.marqueeEnabled;
  p.marqueeScreenName = s.marquee.marqueeScreenName;
  p.marqueeMode = s.marquee.marqueeMode;
  p.bootSplashEnabled = s.splash.bootSplashEnabled;
  p.resumeFocusSplashEnabled = s.splash.resumeFocusSplashEnabled;
  p.bootSplashTitle = s.splash.bootSplashTitle;
  p.bootSplashSubtitle = s.splash.bootSplashSubtitle;
  p.resumeFocusSplashTitle = s.splash.resumeFocusSplashTitle;
  p.resumeFocusSplashSubtitle = s.splash.resumeFocusSplashSubtitle;
  return p;
}

ErrorUtils::Result<QList<PresentationProfile>> loadRegistry(const QString &filePath) {
  QList<PresentationProfile> out;
  if (filePath.isEmpty()) return out;
  QFile f(filePath);
  if (!f.exists()) return out;
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileReadError,
                               "Failed to open presentation profile registry",
                               "PresentationProfileIO::loadRegistry")
        .withDetails(f.errorString());
  }
  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Presentation profile registry is malformed JSON",
                               "PresentationProfileIO::loadRegistry")
        .withDetails(err.errorString());
  }
  // Same shape tolerance as the layout-profile registry — bare array or
  // wrapped {"profiles": [...]} both load.
  QJsonArray arr;
  if (doc.isArray()) {
    arr = doc.array();
  } else if (doc.isObject() && doc.object().value("profiles").isArray()) {
    arr = doc.object().value("profiles").toArray();
  } else {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Presentation profile registry root is not an array",
                               "PresentationProfileIO::loadRegistry");
  }
  for (const auto &v : arr) {
    if (!v.isObject()) continue;
    auto parsed = fromJson(v.toObject());
    if (parsed.isOk()) out.append(parsed.value());
  }
  return out;
}

ErrorUtils::Result<bool> saveRegistry(const QList<PresentationProfile> &profiles,
                                      const QString &filePath) {
  if (filePath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidFilePath,
                               "Cannot save presentation profile registry to an empty path",
                               "PresentationProfileIO::saveRegistry");
  }
  const QString parentDir = QFileInfo(filePath).absolutePath();
  if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to create parent directory for registry",
                               "PresentationProfileIO::saveRegistry");
  }
  QJsonArray arr;
  for (const PresentationProfile &p : profiles) arr.append(toJson(p));
  QJsonObject root;
  root["schemaVersion"] = kCurrentSchemaVersion;
  root["profiles"] = arr;

  QSaveFile f(filePath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to open presentation profile registry for writing",
                               "PresentationProfileIO::saveRegistry")
        .withDetails(f.errorString());
  }
  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (f.write(bytes) != bytes.size()) {
    f.cancelWriting();
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Short write on presentation profile registry",
                               "PresentationProfileIO::saveRegistry");
  }
  if (!f.commit()) {
    return ErrorContext::error(ErrorCode::FileWriteError,
                               "Failed to commit presentation profile registry write",
                               "PresentationProfileIO::saveRegistry");
  }
  PathUtils::syncDirectory(parentDir);
  return true;
}

QList<PresentationProfile> addOrReplace(const QList<PresentationProfile> &registry,
                                        const PresentationProfile &profile) {
  const QString trimmed = profile.name.trimmed();
  if (trimmed.isEmpty()) return registry;
  QList<PresentationProfile> out;
  out.reserve(registry.size() + 1);
  bool replaced = false;
  for (const PresentationProfile &existing : registry) {
    if (existing.name.trimmed().compare(trimmed, Qt::CaseInsensitive) == 0) {
      out.append(profile);
      replaced = true;
    } else {
      out.append(existing);
    }
  }
  if (!replaced) out.append(profile);
  return out;
}

QList<PresentationProfile> removeByName(const QList<PresentationProfile> &registry,
                                        const QString &name) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) return registry;
  QList<PresentationProfile> out;
  out.reserve(registry.size());
  for (const PresentationProfile &existing : registry) {
    if (existing.name.trimmed().compare(trimmed, Qt::CaseInsensitive) != 0) {
      out.append(existing);
    }
  }
  return out;
}

} // namespace PresentationProfileIO
