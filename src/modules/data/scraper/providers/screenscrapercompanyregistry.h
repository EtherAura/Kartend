#ifndef SCREENSCRAPERCOMPANYREGISTRY_H
#define SCREENSCRAPERCOMPANYREGISTRY_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "errorutils.h"

struct CollectionConfig;

/// On-disk registry of ScreenScraper company id → display name pairs,
/// accumulated from jeuInfos responses as games are scraped (Kartend-cnti4).
///
/// WHY IT EXISTS: SS has no company listing endpoint (verified live,
/// Kartend-13co2), so a name like "Nintendo" can never be resolved to a
/// companyid by asking the API. The ONLY place the id↔name link appears is a
/// game's editeur/developpeur object — which the parser used to throw away
/// (Kartend-p1k3g). Recording every pair we ever see is what lets a
/// hand-made manufacturer parent collection be matched, by NAME, to the
/// `_shared/<type>/company_<id>.<ext>` art that game scrapes already put on
/// disk (user-approved design 2026-08-17: case-insensitive name match,
/// persisted like the children).
///
/// Mirrors the systems/mediatypes cache modules: pure JSON parse + on-disk
/// read/write, no network, no QApplication — the recording call lives in the
/// provider. Unlike those caches there is no TTL: entries are facts, not a
/// refreshable catalog, and the file only ever grows (a few hundred entries
/// for a large library; bytes, not megabytes).
namespace ScreenScraperCompanyRegistry {

/// Canonical path: `CacheLocation/kartend/screenscraper-companies.json`.
[[nodiscard]] QString defaultPath();

/// id → display name. Ids are pre-validated by the parser's scope-key
/// allowlist before they ever reach a ScrapedItem, but load() re-filters so
/// a hand-edited file can't smuggle a path component into a filename either.
using CompanyMap = QHash<QString, QString>;

[[nodiscard]] ErrorUtils::Result<CompanyMap> parse(const QByteArray &json);

/// Missing file is an empty map (first run), not an error; only a parse
/// failure errors, so callers can distinguish "nothing recorded yet" from
/// "file corrupted" — same contract as the sibling caches.
[[nodiscard]] ErrorUtils::Result<CompanyMap> load(const QString &filePath);

[[nodiscard]] bool save(const QString &filePath, const CompanyMap &companies);

/// Merge one observed pair into @p into. Returns true when the map CHANGED
/// (new id, or a previously-empty name gained text) — the provider uses this
/// to write the file only when there is something new, so a 20k-item scrape
/// does not rewrite an unchanged registry 20k times. An id that already has
/// a non-empty name keeps it: SS occasionally localises company text, and
/// first-seen is as good as any without a canonical source to prefer.
bool merge(CompanyMap &into, const QString &id, const QString &name);

/// Company ids whose recorded name matches @p name (trimmed,
/// case-insensitive): exact matches first, then word-boundary PREFIX matches
/// ("Sony" also claims "Sony Computer Entertainment" — SS publisher entities
/// carry the corporate long form, so first-party manufacturers rarely equal
/// their colloquial collection name). Several ids can share a name — SS has
/// distinct ids for regional arms — so all matches are returned in
/// exact-then-prefix order and the caller tries each until art is found.
[[nodiscard]] QStringList idsForName(const CompanyMap &companies, const QString &name);

/// The full name → art resolution (Kartend-cnti4, user-approved design):
/// case-insensitive match of @p collectionName against recorded company
/// names, then the first id (in sorted order) with art on disk wins. Empty
/// when the name matches no recorded company or no matched id has art yet —
/// both are normal early states that resolve as more games get scraped.
[[nodiscard]] QString logoForCollectionName(const CompanyMap &companies,
                                            const QString &collectionName,
                                            const QStringList &artworkRoots);

/// Find on-disk company art for @p companyId under the `_shared/` tree of
/// each root in @p artworkRoots (searched in order). Company assets are
/// written as `_shared/<type>/company_<id>.<ext>` by the game-scrape media
/// router; type preference is pictocouleur (the colour logo SS serves for
/// companies) before pictomonochrome before any other type carrying the id.
/// Returns the first hit, or empty when nothing has been scraped yet.
[[nodiscard]] QString findCompanyArt(const QStringList &artworkRoots, const QString &companyId);

/// The manufacturer-logo matching pass over a whole collection list
/// (Kartend-cnti4): for each non-playlist collection whose name matches a
/// recorded company, wire the on-disk company art into collectionIcon +
/// headerLogoImage. Only empty or company-art-owned slots are written —
/// platform art and user-chosen images are never displaced. Returns true
/// when any collection changed (caller persists). Lives here rather than on
/// the scrape coordinator so it can ALSO run at app startup (field report
/// 2026-08-17: art/registry landed on disk while the app was running, and
/// only a scrape-completion would ever have wired it; a disk-only pass at
/// boot needs no service context and no network).
bool applyToCollections(QList<CollectionConfig> &collections, const QString &registryPath);

/// First on-disk COLLECTION-scoped logo for @p collectionUuid
/// (`_shared/<type>/collection_<uuid>.<ext>`, preferring the vector) across
/// @p artworkRoots — the shape the Wikidata fallback and TMDB collection art
/// write. Consumed by applyToCollections as the second probe after the
/// company name-match.
[[nodiscard]] QString findCollectionArt(const QStringList &artworkRoots,
                                        const QString &collectionUuid);

} // namespace ScreenScraperCompanyRegistry

#endif // SCREENSCRAPERCOMPANYREGISTRY_H
