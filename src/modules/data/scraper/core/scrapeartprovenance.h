#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "scrapertypes.h"

/// Provenance of the art Kartend writes under a collection's `_shared/`
/// directory, and the catalogue-hash fetch plan built from it (Kartend-twq6j).
///
/// A platform re-scrape used to download every asset the catalogue advertises
/// just to byte-compare it against the file already on disk — ~4.8 MB per
/// system to write nothing. The catalogue publishes an MD5 for each asset, so
/// the comparison can be made BEFORE the request. Three outcomes per asset
/// (user decision 2026-08-31 on Kartend-twq6j):
///   * on-disk hash == catalogue hash          → skip the request, wire the file;
///   * on-disk hash == what WE last wrote, and the catalogue moved on
///                                              → re-download (it is our file);
///   * on-disk hash matches neither            → the user replaced the art
///                                                by hand; leave it alone.
/// The middle case needs to know what we wrote — that is the record kept in
/// `<artworkDir>/_shared/provenance.json`, keyed by path relative to
/// `_shared/` ("wheel/platform_4.png") with the lowercase hex MD5 of the bytes.
/// Files present before the record existed have no entry; when such a file
/// differs from the catalogue it is treated as the user's (kept), which errs
/// on the side of never clobbering — RescrapeMode::Overwrite still refreshes
/// everything and bypasses this plan entirely.
namespace Scraper::ArtProvenance {

/// `<artworkDir>/_shared/provenance.json`.
QString provenanceFilePath(const QString &artworkDir);

/// Path of @p absolutePath relative to `<artworkDir>/_shared/`, or empty when
/// the file does not lie inside that directory.
QString sharedRelativePath(const QString &artworkDir, const QString &absolutePath);

/// Recorded hashes, relative path → md5. Empty when there is no record or it
/// cannot be parsed: a corrupt file reads as "nothing recorded", which only
/// makes the plan more conservative (see keptLocalPaths).
QHash<QString, QString> loadRecordedHashes(const QString &artworkDir);

/// Merge @p hashes (relative path → md5) into the record and rewrite it
/// atomically; entries not mentioned are kept. No-op (true) when empty.
[[nodiscard]] bool recordHashes(const QString &artworkDir, const QHash<QString, QString> &hashes);

/// Hash every file in @p absolutePaths that lies under `_shared/` and record
/// it — the write continuation's one call after writeMediaFiles. Paths outside
/// `_shared/` (per-game art) are ignored.
[[nodiscard]] bool recordWrittenFiles(const QString &artworkDir, const QStringList &absolutePaths);

/// What a platform-art job still has to fetch, and what it can settle from
/// disk without a request.
struct FetchPlan {
  /// Assets that need the media-host request (no file, no catalogue hash,
  /// unreadable file, or our own file that the catalogue has since changed).
  QList<MediaAsset> fetch;
  /// Files whose hash equals the catalogue's — already current; skip the
  /// request but still wire them into the collection config.
  QStringList upToDatePaths;
  /// Files matching neither the catalogue nor our record — the user's own
  /// art. Left untouched, still wired into the config.
  QStringList keptLocalPaths;
  /// Relative path → md5 for every up-to-date file: a catalogue match proves
  /// the bytes are the provider's, so they can be recorded as ours even when
  /// the file predates the record.
  QHash<QString, QString> confirmed;
};

/// Sort @p assets into the plan above against @p artworkDir. Reads and hashes
/// files only — never writes — and is pure enough to unit-test with a temp
/// dir. Hashing is real I/O: run it off the GUI thread.
FetchPlan planFetches(const QList<MediaAsset> &assets, const QString &artworkDir);

} // namespace Scraper::ArtProvenance
