#ifndef DATLIBRARYSCAN_H
#define DATLIBRARYSCAN_H

#include <QList>
#include <QSet>
#include <QString>

#include "datcollectionmatch.h"

/// DAT-library folder scan (Kartend-m6qsb.5): enumerate the catalogue files
/// under a watched folder, identify each via a cheap header probe (never an
/// ingest — a library of 100MB listxmls must scan in moments), and rank the
/// existing collections each one plausibly belongs to. Produces PROPOSALS
/// only; attaching is the review dialog's confirm-driven job.
///
/// Pure apart from filesystem reads, so it unit-tests against temp trees and
/// runs safely on a worker thread at startup.
namespace DatLibraryScan {

/// One DAT the user should be asked about: it matched at least one
/// collection, is attached to none, and was not dismissed at this revision.
struct Proposal {
  QString datPath;       ///< Absolute path as enumerated.
  QString canonicalPath; ///< Canonical form — keys dismissals.
  qint64 mtimeMs = 0;    ///< Revision stamp the dismissal pairs with.
  QString headerName;    ///< From the header probe; may be empty (UI falls back to the basename).
  QString headerVersion;
  QList<DatCollectionMatch::Candidate> candidates; ///< Non-empty by construction.
};

struct ScanResult {
  QList<Proposal> proposals;
  /// Valid catalogues in the library that the matcher found NO collection for,
  /// aren't attached anywhere, and weren't dismissed — surfaced on demand
  /// ("show unmatched", Kartend-m6qsb.24) so the user can attach them by hand.
  /// Their `candidates` list is empty.
  QList<Proposal> unmatched;
  int scannedDats = 0; ///< Files that probed as parseable DATs (for the status line).
};

/// Walk `libraryRoot` recursively for *.dat / *.xml files and propose
/// matches. `dismissedKeys` holds DatLibraryState::dismissalKey() strings —
/// a dismissed (path, mtime) pair is skipped, but the same path with a newer
/// mtime is a new catalogue revision and proposes again. Files whose header
/// probe yields Dialect::Unknown are silently skipped (stray XML in the
/// folder is expected, not an error). Matching runs on header name /
/// description / filename only — record extensions would need a full ingest,
/// which a startup scan must never pay.
[[nodiscard]] ScanResult scan(const QString &libraryRoot,
                              const QList<DatCollectionMatch::CollectionInfo> &collections,
                              const QSet<QString> &dismissedKeys);

} // namespace DatLibraryScan

#endif // DATLIBRARYSCAN_H
