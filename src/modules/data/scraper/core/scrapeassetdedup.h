#ifndef KARTEND_SCRAPE_ASSET_DEDUP_H
#define KARTEND_SCRAPE_ASSET_DEDUP_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "scrapertypes.h"

/// Pure, UI-free helpers for the scrape-download dedup / CRC short-circuit
/// pipeline (Kartend-dpehr). Extracted from ScrapeResultDialog so the dedup
/// probes, local-file hashing, and ScreenScraper hash-hint URL / short-circuit
/// logic can be unit-tested without instantiating a QDialog. The dialog (and
/// any non-interactive batch path) calls these; they own no state.
namespace ScrapeAssetDedup {

/// CRC32 / MD5 / SHA1 hex digests of a local file for SS's hash short-circuit.
struct LocalHashes {
  QString crc32Hex; ///< SS expects CRC32 as hex; left empty (Qt ships no CRC32).
  QString md5Hex;
  QString sha1Hex;
};

/// On-disk paths a group/company-scoped asset could occupy under @p artworkDir
/// (`_shared/<type>/<scope>_<scopeKey>.<ext>` across common image extensions).
/// Empty for Game-scoped assets or when inputs are empty.
QStringList sharedAssetProbePaths(const Scraper::MediaAsset &asset, const QString &artworkDir);

/// First existing shared-asset file across @p searchPaths, or empty.
QString findExistingSharedAsset(const Scraper::MediaAsset &asset, const QStringList &searchPaths);

/// First existing per-game (Game-scoped) image file under
/// `{artworkDir}/<type>/{baseName}.<ext>`, or empty. Skips video/manual types
/// (SS doesn't accept hash params on those endpoints).
QString findExistingPerGameAsset(const Scraper::MediaAsset &asset, const QString &artworkDir,
                                 const QString &baseName);

/// Stream @p path once through MD5 + SHA1. Empty result when unreadable.
LocalHashes hashLocalFile(const QString &path);

/// Append SS md5/sha1/crc query params to @p original so the server can reply
/// with a tiny sentinel instead of the full bytes when the local file matches.
/// Returns @p original unchanged when no hashes are present.
QUrl withHashHints(const QUrl &original, const LocalHashes &hashes);

/// True when @p body is one of SS's short-circuit sentinels
/// (MD5OK / SHA1OK / CRCOK), tolerating trailing whitespace.
bool isHashShortCircuit(const QByteArray &body);

} // namespace ScrapeAssetDedup

#endif // KARTEND_SCRAPE_ASSET_DEDUP_H
