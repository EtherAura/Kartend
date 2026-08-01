// SS systemesListe.php cache. Pure JSON parser + on-disk read/write
// + age check. The actual HTTP fetch lives in the provider — this
// module only handles the data shape so it stays testable without
// a network or a QApplication.
#include "screenscrapersystemcache.h"

#include "screenscraperjsoncache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ScreenScraperSystemCache {

namespace {

constexpr const char *CACHE_FILE_NAME = "screenscraper-systems.json";

/// Walk every string-valued field under the `noms` object and
/// collect the values as candidate aliases. SS exposes many name
/// variants per system (nom_eu / nom_us / nom_recalbox / nom_retropie
/// / nom_launchbox / nom_hyperspin / etc.); we don't enumerate them
/// — we just take everything string-valued so future SS additions
/// are picked up automatically. Multi-element string arrays (some
/// `noms_*` fields are arrays) are flattened in too.
QStringList extractAliases(const QJsonObject &noms) {
  QStringList out;
  out.reserve(noms.size() * 2);
  QSet<QString> seen;
  auto add = [&out, &seen](const QString &raw) {
    const QString trimmed = raw.trimmed().toLower();
    if (trimmed.isEmpty()) return;
    if (seen.contains(trimmed)) return;
    seen.insert(trimmed);
    out.append(trimmed);
  };
  for (auto it = noms.constBegin(); it != noms.constEnd(); ++it) {
    const QJsonValue v = it.value();
    if (v.isString()) {
      add(v.toString());
    } else if (v.isArray()) {
      for (const auto &elem : v.toArray()) {
        if (elem.isString()) {
          add(elem.toString());
        }
      }
    }
  }
  return out;
}

/// Split SS's comma-separated `extensions` string into the typed
/// QStringList shape. Extensions are stored without dots, lowercase
/// — so "smc,sfc,7z" → {"smc","sfc","7z"}.
QStringList splitExtensions(const QString &raw) {
  QStringList out;
  for (const QString &part : raw.split(',', Qt::SkipEmptyParts)) {
    QString trimmed = part.trimmed().toLower();
    if (trimmed.startsWith('.')) trimmed.remove(0, 1);
    if (!trimmed.isEmpty() && !out.contains(trimmed)) {
      out.append(trimmed);
    }
  }
  return out;
}

/// Pick the first non-empty preferred field from `noms` for the
/// display name. We check a small set of preferred keys (eu / us /
/// commun) and fall back to the first string-valued entry. SS-
/// supplied; we don't invent the strings.
QString pickDisplayName(const QJsonObject &noms) {
  for (const QString &key :
       {QStringLiteral("nom_eu"), QStringLiteral("nom_us"), QStringLiteral("noms_commun")}) {
    const QJsonValue v = noms.value(key);
    if (v.isString() && !v.toString().trimmed().isEmpty()) {
      return v.toString().trimmed();
    }
    if (v.isArray() && !v.toArray().isEmpty()) {
      const QString first = v.toArray().first().toString().trimmed();
      if (!first.isEmpty()) return first;
    }
  }
  // Last-resort fallback: the first string we find.
  for (auto it = noms.constBegin(); it != noms.constEnd(); ++it) {
    if (it.value().isString()) {
      const QString s = it.value().toString().trimmed();
      if (!s.isEmpty()) return s;
    }
  }
  return {};
}

/// SS responses sometimes shape extensions as an array, sometimes as
/// a comma-separated string. Normalise.
QStringList readExtensions(const QJsonValue &v) {
  if (v.isString()) return splitExtensions(v.toString());
  if (v.isArray()) {
    QStringList out;
    for (const auto &elem : v.toArray()) {
      QString e = elem.toString().trimmed().toLower();
      if (e.startsWith('.')) e.remove(0, 1);
      if (!e.isEmpty() && !out.contains(e)) {
        out.append(e);
      }
    }
    return out;
  }
  return {};
}

} // namespace

QString defaultCachePath() {
  return ScreenScraperJsonCache::cachePath(CACHE_FILE_NAME);
}

ErrorUtils::Result<QList<ScreenScraperSystems::System>>
parseSystemsResponse(const QByteArray &json) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "ScreenScraper systemesListe response is not valid JSON",
                               "ScreenScraperSystemCache::parseSystemsResponse")
        .withDetails(err.errorString());
  }
  // SS wraps the actual list inside response.systemes. Support both
  // the wrapped shape (live API) and the raw-array shape (some test
  // payloads or future SS variants).
  const QJsonObject root = doc.object();
  const QJsonArray systemes = ScreenScraperJsonCache::unwrapArray(root, "systemes");

  QList<ScreenScraperSystems::System> out;
  out.reserve(systemes.size());
  for (const auto &v : systemes) {
    const QJsonObject sys = v.toObject();
    if (sys.isEmpty()) continue;
    ScreenScraperSystems::System s;
    // SS returns the id as either a JSON number or a string-of-int
    // depending on the endpoint variant. Handle both.
    const QJsonValue idValue = sys.value("id");
    if (idValue.isDouble()) {
      s.id = static_cast<int>(idValue.toDouble(-1));
    } else {
      bool ok = false;
      s.id = idValue.toString().toInt(&ok);
      if (!ok) s.id = -1;
    }
    if (s.id < 0) continue;
    const QJsonObject noms = sys.value("noms").toObject();
    s.displayName = pickDisplayName(noms);
    s.aliases = extractAliases(noms);
    s.extensions = readExtensions(sys.value("extensions"));
    out.append(s);
  }
  return out;
}

ErrorUtils::Result<QList<ScreenScraperSystems::System>> loadCachedSystems(const QString &filePath) {
  if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
    return QList<ScreenScraperSystems::System>{};
  }
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::warning(ErrorCode::FileNotFound,
                                 "Failed to open ScreenScraper system cache",
                                 "ScreenScraperSystemCache::loadCachedSystems")
        .withDetails(f.errorString());
  }
  const QByteArray bytes = f.readAll();
  f.close();
  return parseSystemsResponse(bytes);
}

bool saveSystems(const QString &filePath, const QList<ScreenScraperSystems::System> &systems) {
  // Write as the same `systemes[]` shape our parser reads — round-
  // trippable so a power user inspecting the file can edit it
  // without learning a separate schema.
  QJsonArray systemes;
  for (const auto &s : systems) {
    QJsonObject sys;
    sys["id"] = s.id;
    QJsonObject noms;
    noms["nom_kartend"] = s.displayName;
    QJsonArray aliasesArr;
    for (const QString &a : s.aliases) aliasesArr.append(a);
    noms["aliases"] = aliasesArr;
    sys["noms"] = noms;
    sys["extensions"] = s.extensions.join(QChar(','));
    systemes.append(sys);
  }
  QJsonObject root;
  root["systemes"] = systemes;
  return ScreenScraperJsonCache::writeJsonCompact(filePath, root,
                                                  "ScreenScraperSystemCache::saveSystems",
                                                  "Failed to write ScreenScraper cache");
}

bool isCacheStale(const QString &filePath) {
  return ScreenScraperJsonCache::isStale(filePath, CACHE_TTL_DAYS);
}

} // namespace ScreenScraperSystemCache
