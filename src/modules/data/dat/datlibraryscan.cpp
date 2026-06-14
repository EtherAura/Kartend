#include "datlibraryscan.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include "datlibrarystate.h"
#include "datlookup.h"

namespace DatLibraryScan {

namespace {

// Sanity cap on enumerated candidate files. A library folder is a curated
// set of catalogues (dozens, maybe hundreds); hitting this means the user
// pointed the setting at something huge, and scanning on is just noise.
constexpr int kMaxFilesScanned = 5000;

} // namespace

ScanResult scan(const QString &libraryRoot,
                const QList<DatCollectionMatch::CollectionInfo> &collections,
                const QSet<QString> &dismissedKeys) {
  ScanResult out;
  const QString root = libraryRoot.trimmed();
  if (root.isEmpty() || !QFileInfo(root).isDir() || collections.isEmpty()) {
    return out;
  }

  QDirIterator it(root, {QStringLiteral("*.dat"), QStringLiteral("*.xml")},
                  QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  int seen = 0;
  while (it.hasNext() && seen < kMaxFilesScanned) {
    const QString path = it.next();
    ++seen;
    const QFileInfo fi = it.fileInfo();
    const QString canonical = fi.canonicalFilePath();
    const qint64 mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    if (dismissedKeys.contains(
            DatLibraryState::dismissalKey(canonical.isEmpty() ? path : canonical, mtimeMs))) {
      continue; // dismissed at this exact revision
    }

    // Header-only identification — bounded prefix read, never an ingest.
    const DatLookup::DatHeader header = DatLookup::probeHeaderFromFile(path);
    if (header.dialect == DatLookup::Dialect::Unknown) {
      continue; // stray XML / not a catalogue — expected in real folders
    }
    ++out.scannedDats;

    DatCollectionMatch::DatInfo info;
    info.path = path;
    info.headerName = header.name;
    info.headerDescription = header.description;
    // romExtensions deliberately left empty: deriving them needs the record
    // list (a full ingest). The matcher treats an empty side as neutral.

    const DatCollectionMatch::MatchResult ranked =
        DatCollectionMatch::rankCollections(info, collections);
    if (!ranked.alreadyAttachedTo.isEmpty()) {
      continue; // already attached — a fact, not a question
    }

    Proposal p;
    p.datPath = path;
    p.canonicalPath = canonical.isEmpty() ? path : canonical;
    p.mtimeMs = mtimeMs;
    p.headerName = header.name;
    p.headerVersion = header.version;
    p.candidates = ranked.candidates;
    // Matched → a proposal; matched nothing → unmatched (surfaced on demand so
    // the user can attach it by hand, Kartend-m6qsb.24).
    if (ranked.candidates.isEmpty()) {
      out.unmatched.append(p);
    } else {
      out.proposals.append(p);
    }
  }
  return out;
}

} // namespace DatLibraryScan
