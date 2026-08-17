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
#include <QUrl>
#include <QUrlQuery>

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

/// SS is inconsistent about scalar shape across endpoint variants — the
/// same field arrives as a JSON string on one and a number on another
/// (`id` already needed this treatment below). Normalise to trimmed text;
/// anything non-scalar reads as empty.
QString readScalarText(const QJsonValue &v) {
  if (v.isString()) return v.toString().trimmed();
  if (v.isDouble()) return QString::number(v.toDouble());
  return {};
}

/// Pull the `media=` token and the endpoint kind out of a medias[] url,
/// discarding everything else — crucially the devid / devpassword / ssid /
/// sspassword SS interpolates into every url it hands back. Nothing from
/// the query string other than the media token survives this function, so
/// no credential can reach the cache file (Kartend-xny9o).
void readMediaUrl(const QString &rawUrl, ScreenScraperSystems::Media &m) {
  if (rawUrl.isEmpty()) return;
  const QUrl url(rawUrl);
  m.token = QUrlQuery(url).queryItemValue(QStringLiteral("media"), QUrl::FullyDecoded).trimmed();
  // Video assets come from a sibling endpoint taking the same params. An
  // endpoint we don't recognise stays video=false — the entry's type and
  // hashes are still worth keeping, and a caller that can't rebuild the
  // URL simply falls back to requesting the type by name.
  m.video = url.path().endsWith(QLatin1String("mediaVideoSysteme.php"), Qt::CaseInsensitive);
}

/// Parse one system's `medias` array. Tolerates BOTH shapes the way
/// readExtensions/unwrapArray already do: the live SS response (a `url`
/// carrying the media token) and our own round-tripped cache (`media` +
/// `video` written back directly, since the url was never stored).
QList<ScreenScraperSystems::Media> readMedia(const QJsonValue &v) {
  const QJsonArray arr = v.toArray();
  QList<ScreenScraperSystems::Media> out;
  out.reserve(arr.size());
  for (const auto &elem : arr) {
    const QJsonObject o = elem.toObject();
    if (o.isEmpty()) continue;
    ScreenScraperSystems::Media m;
    m.type = readScalarText(o.value("type")).toLower();
    if (m.type.isEmpty()) continue; // an untyped asset is unaddressable
    if (o.contains(QStringLiteral("url"))) {
      readMediaUrl(readScalarText(o.value("url")), m);
    } else {
      m.token = readScalarText(o.value("media"));
      m.video = o.value("video").toBool(false);
    }
    // SS always qualifies the token by region; a cache file hand-edited
    // to drop it can still address the asset by bare type.
    if (m.token.isEmpty()) m.token = m.type;
    m.region = readScalarText(o.value("region")).toLower();
    m.support = readScalarText(o.value("support"));
    m.format = readScalarText(o.value("format")).toLower();
    m.crc = readScalarText(o.value("crc"));
    m.md5 = readScalarText(o.value("md5"));
    m.sha1 = readScalarText(o.value("sha1"));
    out.append(m);
  }
  return out;
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
    // Retained-verbatim catalog fields (Kartend-xny9o). Every one of these
    // is optional in the live response — `compagnie` alone is absent from
    // roughly a third of the catalog — so each simply reads empty when SS
    // omits it. None of them can fail the entry.
    s.company = readScalarText(sys.value("compagnie"));
    s.systemType = readScalarText(sys.value("type"));
    s.startDate = readScalarText(sys.value("datedebut"));
    s.endDate = readScalarText(sys.value("datefin"));
    s.romType = readScalarText(sys.value("romtype"));
    s.supportType = readScalarText(sys.value("supporttype"));
    s.media = readMedia(sys.value("medias"));
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
    // Retained catalog fields, under SS's own key names so the round-trip
    // stays a no-op and a power user editing the file sees the same
    // vocabulary as the API. Omitted entirely when empty — a third of the
    // catalog has no `compagnie`, and writing "" for all of them would
    // bloat the file to no purpose.
    const auto put = [&sys](const char *key, const QString &value) {
      if (!value.isEmpty()) sys[QString::fromLatin1(key)] = value;
    };
    put("compagnie", s.company);
    put("type", s.systemType);
    put("datedebut", s.startDate);
    put("datefin", s.endDate);
    put("romtype", s.romType);
    put("supporttype", s.supportType);
    if (!s.media.isEmpty()) {
      QJsonArray medias;
      for (const auto &m : s.media) {
        QJsonObject mo;
        mo["type"] = m.type;
        // `media` (the token), NOT `url` — the url SS gave us carried
        // devid/devpassword/ssid/sspassword and was discarded at parse
        // time. Writing one here would put the dev password on disk in
        // cleartext; the round-trip test asserts it does not.
        mo["media"] = m.token;
        if (m.video) mo["video"] = true;
        const auto putM = [&mo](const char *key, const QString &value) {
          if (!value.isEmpty()) mo[QString::fromLatin1(key)] = value;
        };
        putM("region", m.region);
        putM("support", m.support);
        putM("format", m.format);
        putM("crc", m.crc);
        putM("md5", m.md5);
        putM("sha1", m.sha1);
        medias.append(mo);
      }
      sys["medias"] = medias;
    }
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
