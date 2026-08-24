#ifndef WIKIDATALOGOPARSER_H
#define WIKIDATALOGOPARSER_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QUrl>

#include "errorutils.h"

/// Pure request-building + response-parsing helpers for the Wikidata /
/// Wikimedia Commons logo lookup (Kartend-czna3): collection NAME →
/// Wikidata entity (wbsearchentities) → P154 "logo image" claim
/// (wbgetclaims) → Commons file URL (Special:FilePath, which redirects to
/// the ORIGINAL upload — an SVG stays an SVG, per the user's requirement
/// that vector logos are downloaded and styled at render time, not
/// server-rasterised). Kept parser-pure so the whole wire shape is
/// unit-testable with canned JSON; the provider owns the HTTP.
namespace WikidataLogoParser {

/// wbsearchentities for @p name (English, items only). Empty URL for an
/// empty/whitespace name.
[[nodiscard]] QUrl buildSearchUrl(const QString &name);

/// One wbsearchentities hit — id plus the label/description the picker
/// scores (Kartend-6i10t: "Saturn" must resolve to the console, not the
/// planet, for a games collection).
struct SearchHit {
  QString id;
  QString label;
  QString description;
  /// The label/alias text wbsearchentities actually matched the query
  /// against ("Sony" for Sony Group's alias; "Sony PlayStation 3" when the
  /// query merely prefixes a longer name). Exactness of this against the
  /// query is the picker's primary rank — it is what stops a games
  /// collection named after a COMPANY from resolving to that company's
  /// console (Kartend-5b5r1 follow-up: "Sony" scraped as PS3).
  QString matchText;
};
/// Every hit from a wbsearchentities response, in ranking order.
[[nodiscard]] ErrorUtils::Result<QList<SearchHit>> parseEntitySearchHits(const QByteArray &json);
/// The queries worth trying for @p name, most specific first: the full
/// name, then fragments of compound names ("Famicom - Nintendo
/// Entertainment System" → "Famicom", "Nintendo Entertainment System") —
/// those compound labels match no Wikidata entity as-is.
[[nodiscard]] QStringList searchQueryLadder(const QString &name);
/// The hit that plausibly IS the collection: first one whose label or
/// description matches the media-type keyword set derived from
/// @p collectionType ("Games" → console/video-game vocabulary…), else the
/// generic company vocabulary. Empty when nothing matches — the caller
/// escalates to the next ladder query rather than accepting a wrong
/// entity.
/// @p preferCompany: true for SHELL collections (ones other collections
/// name as parent) — they are named after companies/brands, so company
/// vocabulary outranks the media words. Junk entities (given names,
/// surnames, disambiguation pages) never win regardless of exactness.
[[nodiscard]] QString pickEntityForCollection(const QList<SearchHit> &hits,
                                              const QString &collectionType, const QString &query,
                                              bool preferCompany = false);

/// wbgetclaims for property P154 on @p entityId ("Q8093").
[[nodiscard]] QUrl buildClaimsUrl(const QString &entityId);

/// Commons Special:FilePath URL for @p filename — the stable "give me the
/// file" endpoint that redirects to the original upload host.
[[nodiscard]] QUrl buildLogoFileUrl(const QString &filename);

/// First entity id from a wbsearchentities response. Empty-search results
/// parse as a benign empty string (caller maps to not-found); malformed
/// JSON is an error.
[[nodiscard]] ErrorUtils::Result<QString> parseEntitySearch(const QByteArray &json);

/// The P154 filename from a wbgetclaims response ("Nintendo.svg"). A
/// present-but-claimless response parses as a benign empty string. The
/// filename is VALIDATED here — it becomes both a URL path segment and,
/// indirectly, drives which on-disk type directory the asset lands in — so
/// a value carrying path separators / traversal / a missing extension is
/// rejected as empty rather than propagated. A "File:" prefix is stripped
/// (both raw-string and object claim shapes occur in the wild).
[[nodiscard]] ErrorUtils::Result<QString> parseLogoClaim(const QByteArray &json);

/// True when @p filename is a plain "<base>.<ext>" Commons filename with no
/// separators or traversal. Exposed for tests.
[[nodiscard]] bool isSafeLogoFilename(const QString &filename);

// ── Entity DATA additions (Kartend-445su) ────────────────────────────────
// The logo-only wire shape above stays for compatibility; the data path
// asks wbgetentities for claims + sitelinks + descriptions in one call,
// resolves the manufacturer entity's label, and pulls the prose paragraph
// from Wikipedia's REST summary endpoint via the enwiki sitelink.

/// Everything the data lookup extracts from one wbgetentities response.
/// All fields optional — absence is normal on sparse entities.
struct EntityData {
  QString logoFilename;   ///< P154, validated like parseLogoClaim's result.
  QString manufacturerId; ///< P176's first value ("Q122741"), needs a label hop.
  QString inceptionYear;  ///< P571's year ("1988"); Wikidata time values vary.
  // Kartend-6i10t: the wider fact set. Entity references need the same
  // label hop as the manufacturer — batch them via buildLabelsUrl.
  QString countryId;   ///< P495 country of origin, falling back to P17 country.
  QString developerId; ///< P178 developer.
  QString publisherId; ///< P123 publisher.
  QString genreId;     ///< P136 genre.
  QString websiteUrl;  ///< P856 official website — a plain string, no hop.
  // Kartend-5b5r1: the game-platform spec sheet. Entity references join the
  // batched label hop; quantities and dates are plain values.
  QString cpuId;            ///< P880 CPU.
  QString gpuId;            ///< P2560 GPU.
  QString predecessorId;    ///< P155 follows.
  QString successorId;      ///< P156 followed by.
  QStringList partOfIds;    ///< P361 part of — ALL values; the provider keeps
                            ///< the one whose label names a console generation.
  QString unitsSold;        ///< P2664 quantity, raw digits ("49100000").
  QString publicationYear;  ///< P577 earliest year — release year for consoles.
  QString discontinuedYear; ///< P2669 year; composes the production span.
  QString photoFilename;    ///< P18 first image, validated like the logo.
  /// Every non-empty entity-reference id above, deduplicated, for one
  /// batched wbgetentities labels request.
  [[nodiscard]] QStringList referencedEntityIds() const;
  QString description; ///< The entity's own one-line English description.
  QString enwikiTitle; ///< enwiki sitelink title for the summary endpoint.
};

/// wbgetentities for @p entityId with claims|sitelinks|descriptions, English
/// only, enwiki sitelink only.
[[nodiscard]] QUrl buildEntityDataUrl(const QString &entityId);

/// wbgetentities labels-only for @p entityId (the manufacturer hop).
[[nodiscard]] QUrl buildLabelUrl(const QString &entityId);
/// Batched labels request — one wbgetentities call for every id ("Q1|Q2").
[[nodiscard]] QUrl buildLabelsUrl(const QStringList &entityIds);

/// Wikipedia REST page summary for @p title
/// (https://en.wikipedia.org/api/rest_v1/page/summary/<title>).
[[nodiscard]] QUrl buildWikipediaSummaryUrl(const QString &title);

/// Parse a wbgetentities claims|sitelinks|descriptions response. Sparse
/// fields parse as empty; malformed JSON is an error.
[[nodiscard]] ErrorUtils::Result<EntityData> parseEntityData(const QByteArray &json);

/// The English label from a labels-only wbgetentities response; empty when
/// the entity has none.
[[nodiscard]] ErrorUtils::Result<QString> parseEntityLabel(const QByteArray &json);
/// id → English label for every entity in a (batched) labels response.
/// Entities missing an English label are simply absent from the hash.
[[nodiscard]] ErrorUtils::Result<QHash<QString, QString>> parseEntityLabels(const QByteArray &json);

/// The plain-text `extract` paragraph from a Wikipedia REST summary
/// response; empty when the page has none (disambiguation stubs).
[[nodiscard]] ErrorUtils::Result<QString> parseWikipediaSummary(const QByteArray &json);

} // namespace WikidataLogoParser

#endif // WIKIDATALOGOPARSER_H
