// SS company id → name registry. Pure JSON + file lookup helpers; the
// recording call site lives in ScreenScraperProvider (see header).
#include "screenscrapercompanyregistry.h"

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "pathutils.h"
#include "screenscraperjsoncache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ScreenScraperCompanyRegistry {

namespace {

constexpr const char *REGISTRY_FILE_NAME = "screenscraper-companies.json";

// Same allowlist the parser applies before an id reaches a ScrapedItem
// (screenscraperparser.cpp isValidScopeKey) — duplicated here because load()
// must also distrust a hand-edited registry file: these ids become
// `company_<id>` filename components in findCompanyArt.
bool isValidCompanyId(const QString &s) {
  static const QRegularExpression re(
      QRegularExpression::anchoredPattern(QStringLiteral("[0-9A-Za-z_-]+")));
  return re.match(s).hasMatch();
}

} // namespace

QString defaultPath() {
  return ScreenScraperJsonCache::cachePath(REGISTRY_FILE_NAME);
}

ErrorUtils::Result<CompanyMap> parse(const QByteArray &json) {
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "ScreenScraper company registry is not valid JSON",
                               "ScreenScraperCompanyRegistry::parse")
        .withDetails(err.errorString());
  }
  const QJsonArray companies = doc.object().value(QStringLiteral("companies")).toArray();
  CompanyMap out;
  out.reserve(companies.size());
  for (const auto &v : companies) {
    const QJsonObject o = v.toObject();
    const QString id = o.value(QStringLiteral("id")).toString();
    if (!isValidCompanyId(id)) continue;
    out.insert(id, o.value(QStringLiteral("name")).toString().trimmed());
  }
  return out;
}

ErrorUtils::Result<CompanyMap> load(const QString &filePath) {
  if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
    return CompanyMap{};
  }
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::warning(ErrorCode::FileNotFound,
                                 "Failed to open ScreenScraper company registry",
                                 "ScreenScraperCompanyRegistry::load")
        .withDetails(f.errorString());
  }
  const QByteArray bytes = f.readAll();
  f.close();
  return parse(bytes);
}

bool save(const QString &filePath, const CompanyMap &companies) {
  QJsonArray arr;
  // Sorted for a stable file — the map iterates in hash order, and a registry
  // that reshuffles on every save makes diffs/inspection needlessly noisy.
  QStringList ids = companies.keys();
  ids.sort();
  for (const QString &id : ids) {
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = companies.value(id);
    arr.append(o);
  }
  QJsonObject root;
  root[QStringLiteral("companies")] = arr;
  return ScreenScraperJsonCache::writeJsonCompact(filePath, root,
                                                  "ScreenScraperCompanyRegistry::save",
                                                  "Failed to write ScreenScraper company registry");
}

bool merge(CompanyMap &into, const QString &id, const QString &name) {
  if (!isValidCompanyId(id)) return false;
  const QString trimmed = name.trimmed();
  const auto it = into.constFind(id);
  if (it == into.constEnd()) {
    into.insert(id, trimmed);
    return true;
  }
  if (it.value().isEmpty() && !trimmed.isEmpty()) {
    into.insert(id, trimmed);
    return true;
  }
  return false;
}

QStringList idsForName(const CompanyMap &companies, const QString &name) {
  const QString needle = name.trimmed();
  QStringList out;
  if (needle.isEmpty()) return out;
  // Exact case-insensitive matches first (the user-approved auto path), then
  // WORD-BOUNDARY prefix matches — "Sony" also claims "Sony Computer
  // Entertainment" (field finding 2026-08-17: SS's publisher entities carry
  // the corporate long form, so first-party manufacturers rarely match their
  // colloquial collection name exactly). The trailing space keeps "Sega"
  // from claiming an unrelated "Segasoft-style" name: the prefix must end at
  // a word break. Exact matches sort ahead so a true exact hit always wins.
  QStringList prefixMatches;
  const QString wordPrefix = needle + QLatin1Char(' ');
  for (auto it = companies.constBegin(); it != companies.constEnd(); ++it) {
    if (it.value().compare(needle, Qt::CaseInsensitive) == 0) {
      out.append(it.key());
    } else if (it.value().startsWith(wordPrefix, Qt::CaseInsensitive)) {
      prefixMatches.append(it.key());
    }
  }
  // Deterministic try-order for the caller regardless of hash seed.
  out.sort();
  prefixMatches.sort();
  out += prefixMatches;
  return out;
}

QString logoForCollectionName(const CompanyMap &companies, const QString &collectionName,
                              const QStringList &artworkRoots) {
  for (const QString &id : idsForName(companies, collectionName)) {
    const QString art = findCompanyArt(artworkRoots, id);
    if (!art.isEmpty()) return art;
  }
  return {};
}

QString findCompanyArt(const QStringList &artworkRoots, const QString &companyId) {
  if (!isValidCompanyId(companyId)) return {};
  // pictocouleur first (SS's colour company logo), monochrome fallback —
  // these are the two types jeuInfos actually embeds for editeur/developpeur
  // (verified live, Kartend-13co2). The wildcard pass last, so art under a
  // future type still resolves without a code change.
  const QStringList preferredTypes = {QStringLiteral("pictocouleur"),
                                      QStringLiteral("pictomonochrome")};
  const QString fileGlob = QStringLiteral("company_") + companyId + QStringLiteral(".*");
  for (const QString &root : artworkRoots) {
    if (root.isEmpty()) continue;
    const QDir shared(root + QStringLiteral("/_shared"));
    if (!shared.exists()) continue;
    for (const QString &type : preferredTypes) {
      const QDir typeDir(shared.filePath(type));
      const QStringList hits = typeDir.entryList({fileGlob}, QDir::Files, QDir::Name);
      if (!hits.isEmpty()) return typeDir.filePath(hits.first());
    }
    const QStringList typeDirs = shared.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &type : typeDirs) {
      if (preferredTypes.contains(type)) continue; // already tried, in order
      const QDir typeDir(shared.filePath(type));
      const QStringList hits = typeDir.entryList({fileGlob}, QDir::Files, QDir::Name);
      if (!hits.isEmpty()) return typeDir.filePath(hits.first());
    }
  }
  return {};
}

bool applyToCollections(QList<CollectionConfig> &collections, const QString &registryPath) {
  const auto registry = load(registryPath);
  const CompanyMap companies = registry.isOk() ? registry.value() : CompanyMap{};
  // Shared art lands under the artworkDir of whichever collection's scrape
  // delivered it — for a shell parent that is typically a CHILD or a shared
  // root — so every collection's artwork root participates, deduped.
  QStringList roots;
  for (const CollectionConfig &c : collections) {
    const QString root = PathUtils::validateAndExpandPath(c.artworkDirectory, c.name);
    if (!root.isEmpty() && !roots.contains(root)) roots.append(root);
  }
  // Writable slots: empty, or already carrying matched shared art from an
  // earlier pass (idempotent refresh). Narrower than the entity
  // coordinator's `_shared/` rule on purpose: platform art from the
  // collection's own entity job outranks a match, and a user-chosen image
  // always wins.
  const auto matchWritable = [](const QString &current) {
    return current.isEmpty() || current.contains(QStringLiteral("/company_")) ||
           current.contains(QStringLiteral("/collection_"));
  };
  bool changed = false;
  for (CollectionConfig &cfg : collections) {
    if (cfg.isPlaylist) continue;
    if (!matchWritable(cfg.collectionIcon) && !matchWritable(cfg.background.headerLogoImage)) {
      continue;
    }
    // Company name-match first (SS pictos are curated wheels), then any
    // collection-scoped logo an entity provider (TMDB collection art, the
    // Wikidata fallback, Kartend-czna3) left on disk keyed by this
    // collection's uuid — that second probe is what lets a logo fetched
    // while the app was NOT running get wired at the next boot.
    QString logo = logoForCollectionName(companies, cfg.name, roots);
    if (logo.isEmpty()) {
      logo = findCollectionArt(roots, CollectionUtils::computeCollectionUuid(cfg));
    }
    if (logo.isEmpty()) continue;
    if (matchWritable(cfg.collectionIcon) && cfg.collectionIcon != logo) {
      cfg.collectionIcon = logo;
      changed = true;
    }
    if (matchWritable(cfg.background.headerLogoImage) && cfg.background.headerLogoImage != logo) {
      cfg.background.headerLogoImage = logo;
      changed = true;
    }
  }
  return changed;
}

QString findCollectionArt(const QStringList &artworkRoots, const QString &collectionUuid) {
  if (!isValidCompanyId(collectionUuid)) return {}; // same filename-safety allowlist
  const QStringList preferredTypes = {QStringLiteral("logo-svg"), QStringLiteral("logo"),
                                      QStringLiteral("wheel")};
  const QString fileGlob = QStringLiteral("collection_") + collectionUuid + QStringLiteral(".*");
  for (const QString &root : artworkRoots) {
    if (root.isEmpty()) continue;
    const QDir shared(root + QStringLiteral("/_shared"));
    if (!shared.exists()) continue;
    for (const QString &type : preferredTypes) {
      const QDir typeDir(shared.filePath(type));
      const QStringList hits = typeDir.entryList({fileGlob}, QDir::Files, QDir::Name);
      if (!hits.isEmpty()) return typeDir.filePath(hits.first());
    }
  }
  return {};
}

} // namespace ScreenScraperCompanyRegistry
