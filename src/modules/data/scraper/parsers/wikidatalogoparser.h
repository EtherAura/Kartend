#ifndef WIKIDATALOGOPARSER_H
#define WIKIDATALOGOPARSER_H

#include <QByteArray>
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
  QString description;    ///< The entity's own one-line English description.
  QString enwikiTitle;    ///< enwiki sitelink title for the summary endpoint.
};

/// wbgetentities for @p entityId with claims|sitelinks|descriptions, English
/// only, enwiki sitelink only.
[[nodiscard]] QUrl buildEntityDataUrl(const QString &entityId);

/// wbgetentities labels-only for @p entityId (the manufacturer hop).
[[nodiscard]] QUrl buildLabelUrl(const QString &entityId);

/// Wikipedia REST page summary for @p title
/// (https://en.wikipedia.org/api/rest_v1/page/summary/<title>).
[[nodiscard]] QUrl buildWikipediaSummaryUrl(const QString &title);

/// Parse a wbgetentities claims|sitelinks|descriptions response. Sparse
/// fields parse as empty; malformed JSON is an error.
[[nodiscard]] ErrorUtils::Result<EntityData> parseEntityData(const QByteArray &json);

/// The English label from a labels-only wbgetentities response; empty when
/// the entity has none.
[[nodiscard]] ErrorUtils::Result<QString> parseEntityLabel(const QByteArray &json);

/// The plain-text `extract` paragraph from a Wikipedia REST summary
/// response; empty when the page has none (disambiguation stubs).
[[nodiscard]] ErrorUtils::Result<QString> parseWikipediaSummary(const QByteArray &json);

} // namespace WikidataLogoParser

#endif // WIKIDATALOGOPARSER_H
