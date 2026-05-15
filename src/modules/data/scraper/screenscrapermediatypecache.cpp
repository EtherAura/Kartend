// SS mediasJeuListe.php cache. Pure JSON parser + on-disk read/write
// + age check, mirroring the systemesListe cache. The HTTP fetch
// lives in the provider — this module only handles the data shape so
// it stays testable without a network or a QApplication.
#include "screenscrapermediatypecache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStandardPaths>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ScreenScraperMediaTypeCache {

namespace {

constexpr const char *CACHE_DIR_NAME = "kartend";
constexpr const char *CACHE_FILE_NAME = "screenscraper-mediatypes.json";

/// Pick the best display name from SS's per-locale `nom` array. SS
/// returns entries shaped as `{langue: "en", text: "..."}`. We prefer
/// English, fall back through fr/de/es, and finally take whatever's
/// first non-empty so non-Western SS additions still get a label.
QString pickDisplayName(const QJsonValue &noms) {
  QJsonArray arr;
  if (noms.isArray()) {
    arr = noms.toArray();
  } else if (noms.isObject()) {
    // Some SS variants nest under noms.text; tolerate.
    const QJsonValue inner = noms.toObject().value(QStringLiteral("text"));
    if (inner.isString()) return inner.toString().trimmed();
    return {};
  }
  if (arr.isEmpty()) return {};
  QString firstNonEmpty;
  for (const QString &pref :
       {QStringLiteral("en"), QStringLiteral("fr"), QStringLiteral("de"), QStringLiteral("es")}) {
    for (const auto &v : arr) {
      const QJsonObject obj = v.toObject();
      const QString lang = obj.value(QStringLiteral("langue")).toString().toLower();
      const QString text = obj.value(QStringLiteral("text")).toString().trimmed();
      if (text.isEmpty()) continue;
      if (firstNonEmpty.isEmpty()) firstNonEmpty = text;
      if (lang == pref) return text;
    }
  }
  return firstNonEmpty;
}

/// SS encodes `multiregions`/`multisupports` as either booleans or
/// the strings "true"/"false" depending on the endpoint variant.
bool readBool(const QJsonValue &v) {
  if (v.isBool()) return v.toBool();
  if (v.isString()) {
    const QString s = v.toString().trimmed().toLower();
    return s == QLatin1String("true") || s == QLatin1String("1");
  }
  return false;
}

} // namespace

QString defaultCachePath() {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (base.isEmpty()) return {};
  return base + QLatin1Char('/') + QString::fromLatin1(CACHE_DIR_NAME) + QLatin1Char('/') +
         QString::fromLatin1(CACHE_FILE_NAME);
}

ErrorUtils::Result<QList<MediaType>> parseMediaTypesResponse(const QByteArray &json) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "ScreenScraper mediasJeuListe response is not valid JSON",
                               "ScreenScraperMediaTypeCache::parseMediaTypesResponse")
        .withDetails(err.errorString());
  }
  // SS wraps the list under response.medias; tolerate the raw shape
  // for round-tripped cache files.
  const QJsonObject root = doc.object();
  QJsonArray medias;
  if (root.contains(QStringLiteral("response"))) {
    medias = root.value("response").toObject().value("medias").toArray();
  } else if (root.contains(QStringLiteral("medias"))) {
    medias = root.value("medias").toArray();
  }

  QList<MediaType> out;
  out.reserve(medias.size());
  for (const auto &v : medias) {
    const QJsonObject obj = v.toObject();
    if (obj.isEmpty()) continue;
    MediaType m;
    m.type = obj.value(QStringLiteral("type")).toString().trimmed().toLower();
    if (m.type.isEmpty()) continue;
    m.category = obj.value(QStringLiteral("categorie")).toString().trimmed().toLower();
    m.fileFormat = obj.value(QStringLiteral("fileformat")).toString().trimmed().toLower();
    m.multiRegions = readBool(obj.value(QStringLiteral("multiregions")));
    m.multiSupports = readBool(obj.value(QStringLiteral("multisupports")));
    m.displayName = pickDisplayName(obj.value(QStringLiteral("nom")));
    if (m.displayName.isEmpty()) {
      // Fall back to the canonical tag so the type at least appears in
      // the dialog (the SS tag is usually self-documenting).
      m.displayName = m.type;
    }
    out.append(m);
  }
  return out;
}

ErrorUtils::Result<QList<MediaType>> loadCachedMediaTypes(const QString &filePath) {
  if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
    return QList<MediaType>{};
  }
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::warning(ErrorCode::FileNotFound,
                                 "Failed to open ScreenScraper media-type cache",
                                 "ScreenScraperMediaTypeCache::loadCachedMediaTypes")
        .withDetails(f.errorString());
  }
  const QByteArray bytes = f.readAll();
  f.close();
  return parseMediaTypesResponse(bytes);
}

bool saveMediaTypes(const QString &filePath, const QList<MediaType> &mediaTypes) {
  if (filePath.isEmpty()) return false;
  const QString dir = QFileInfo(filePath).absolutePath();
  if (!QDir().mkpath(dir)) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::FileWriteError,
                              "Failed to create ScreenScraper media-type cache directory",
                              "ScreenScraperMediaTypeCache::saveMediaTypes")
            .withDetails(dir));
    return false;
  }
  // Write as the same `medias[]` shape our parser reads — round-
  // trippable so a power user inspecting the file can edit it without
  // learning a separate schema.
  QJsonArray medias;
  for (const auto &m : mediaTypes) {
    QJsonObject obj;
    obj["type"] = m.type;
    obj["categorie"] = m.category;
    obj["fileformat"] = m.fileFormat;
    obj["multiregions"] = m.multiRegions;
    obj["multisupports"] = m.multiSupports;
    QJsonArray nom;
    QJsonObject e;
    e["langue"] = "en";
    e["text"] = m.displayName;
    nom.append(e);
    obj["nom"] = nom;
    medias.append(obj);
  }
  QJsonObject root;
  root["medias"] = medias;
  QFile f(filePath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::FileWriteError,
                                               "Failed to write ScreenScraper media-type cache",
                                               "ScreenScraperMediaTypeCache::saveMediaTypes")
                             .withDetails(f.errorString()));
    return false;
  }
  f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  f.close();
  return true;
}

bool isCacheStale(const QString &filePath) {
  if (filePath.isEmpty() || !QFileInfo::exists(filePath)) return true;
  const QFileInfo info(filePath);
  const QDateTime modified = info.lastModified();
  if (!modified.isValid()) return true;
  return modified.secsTo(QDateTime::currentDateTime()) >
         static_cast<qint64>(CACHE_TTL_DAYS) * 86400;
}

const MediaType *find(const QList<MediaType> &mediaTypes, const QString &type) {
  const QString needle = type.trimmed().toLower();
  if (needle.isEmpty()) return nullptr;
  for (const auto &m : mediaTypes) {
    if (m.type == needle) return &m;
  }
  return nullptr;
}

} // namespace ScreenScraperMediaTypeCache
