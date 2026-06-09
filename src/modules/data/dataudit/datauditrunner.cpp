#include "datauditrunner.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent/QtConcurrentMap>

#include "datcache.h"
#include "errorutils.h"
#include "filehashcache.h"
#include "romhasher.h"

namespace DatAudit {

namespace {

// One file queued for hashing, carrying the precomputed stat so a cache write
// after hashing doesn't re-stat.
struct PendingHash {
  QString path;
  QString canonical;
  qint64 size = 0;
  qint64 mtimeMs = 0;
};

bool cancelled(const CancelToken &cancel) {
  return cancel && cancel->load();
}

// Compile ignore globs into anchored regexes once, matched against basenames.
QList<QRegularExpression> compileGlobs(const QStringList &globs) {
  QList<QRegularExpression> out;
  out.reserve(globs.size());
  for (const QString &g : globs) {
    if (g.trimmed().isEmpty()) {
      continue;
    }
    out.append(QRegularExpression(
        QRegularExpression::anchoredPattern(QRegularExpression::wildcardToRegularExpression(g)),
        QRegularExpression::CaseInsensitiveOption));
  }
  return out;
}

bool matchesAnyGlob(const QString &name, const QList<QRegularExpression> &globs) {
  for (const QRegularExpression &re : globs) {
    if (re.match(name).hasMatch()) {
      return true;
    }
  }
  return false;
}

// Recursively enumerate regular files under `roots`, skipping basenames that
// match an ignore glob and any path that is one of the audited DAT files.
QStringList enumerateFiles(const AuditOptions &opts) {
  const QList<QRegularExpression> globs = compileGlobs(opts.ignoreGlobs);
  QSet<QString> datCanonical;
  for (const QString &d : opts.datPaths) {
    const QString c = QFileInfo(d).canonicalFilePath();
    if (!c.isEmpty()) {
      datCanonical.insert(c);
    }
  }
  QStringList files;
  QSet<QString> seen; // de-dup overlapping / nested roots
  for (const QString &root : opts.scanRoots) {
    if (root.trimmed().isEmpty()) {
      continue;
    }
    QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString path = it.next();
      const QFileInfo fi = it.fileInfo();
      if (matchesAnyGlob(fi.fileName(), globs)) {
        continue;
      }
      const QString canonical = fi.canonicalFilePath();
      if (!canonical.isEmpty() && datCanonical.contains(canonical)) {
        continue; // never audit the DAT file itself
      }
      const QString key = canonical.isEmpty() ? path : canonical;
      if (seen.contains(key)) {
        continue;
      }
      seen.insert(key);
      files.append(path);
    }
  }
  return files;
}

ScannedFile hashToScanned(const QString &path, const CancelToken &cancel) {
  ScannedFile sf;
  sf.path = path;
  auto r = RomHasher::hashFile(path, cancel);
  if (r.isError()) {
    sf.readOk = false;
    sf.size = -1;
    return sf;
  }
  const RomHasher::Result &hr = r.value();
  sf.crc = hr.crc;
  sf.md5 = hr.md5;
  sf.sha1 = hr.sha1;
  sf.size = hr.size;
  sf.readOk = true;
  return sf;
}

} // namespace

Catalogue buildCatalogue(DatCache::Store &cache, const QStringList &datPaths,
                         QStringList *failedDats) {
  Catalogue cat;
  for (const QString &dat : datPaths) {
    if (dat.trimmed().isEmpty()) {
      continue;
    }
    auto src = cache.openOrIngest(dat);
    if (src.isError()) {
      if (failedDats) {
        failedDats->append(dat);
      }
      continue;
    }
    cache.forEachRecord(src.value(), [&cat](const DatLookup::DatRecord &r) { cat.addRecord(r); });
  }
  return cat;
}

AuditOutput classify(const Catalogue &catalogue, const QList<ScannedFile> &files) {
  AuditOutput out;
  QSet<int> satisfied;
  out.rows.reserve(files.size() + catalogue.size());

  for (const ScannedFile &f : files) {
    AuditRow row;
    row.filePath = f.path;
    row.actualName = QFileInfo(f.path).fileName();
    row.size = f.size;
    row.crc = f.crc;
    row.md5 = f.md5;
    row.sha1 = f.sha1;

    if (!f.readOk) {
      row.status = Status::Corrupt;
      out.rows.append(row);
      continue;
    }

    const int idx = catalogue.matchByHash(f.crc, f.md5, f.sha1);
    if (idx >= 0) {
      const DatLookup::DatRecord &rec = catalogue.record(idx);
      row.gameName = rec.gameName;
      row.expectedName = rec.romName;
      if (satisfied.contains(idx)) {
        row.status = Status::Duplicate;
      } else {
        satisfied.insert(idx);
        row.status = (row.actualName == rec.romName) ? Status::Have : Status::WrongName;
      }
    } else {
      // Content matches nothing. If the name matches a catalogue entry, the
      // file claims to be that entry but holds the wrong bytes (bad/old dump).
      const int nameIdx = catalogue.matchByName(row.actualName);
      if (nameIdx >= 0) {
        const DatLookup::DatRecord &rec = catalogue.record(nameIdx);
        row.gameName = rec.gameName;
        row.expectedName = rec.romName;
        row.status = Status::WrongHash;
      } else {
        row.status = Status::Unknown;
      }
    }
    out.rows.append(row);
  }

  // Every catalogue entry no file satisfied (by content) is Missing.
  for (int i = 0; i < catalogue.size(); ++i) {
    if (satisfied.contains(i)) {
      continue;
    }
    const DatLookup::DatRecord &rec = catalogue.record(i);
    AuditRow row;
    row.status = Status::Missing;
    row.gameName = rec.gameName;
    row.expectedName = rec.romName;
    row.size = rec.size;
    row.crc = rec.crc;
    row.md5 = rec.md5;
    row.sha1 = rec.sha1;
    out.rows.append(row);
  }

  out.summary = summarize(out.rows);
  out.summary.totalCatalogue = catalogue.size();
  out.summary.totalFiles = static_cast<int>(files.size());
  return out;
}

AuditOutput run(const Catalogue &catalogue, const AuditOptions &opts, QSqlDatabase *cacheDb,
                const CancelToken &cancel, const ProgressFn &progress) {
  AuditOutput out;
  const QStringList files = enumerateFiles(opts);
  const int total = static_cast<int>(files.size());
  int done = 0;
  auto tick = [&](const QString &current) {
    if (progress) {
      progress(AuditProgress{done, total, current});
    }
  };

  // Phase 1: cache reads (serial — QSqlDatabase is single-thread). Files with a
  // valid (size,mtime) cache hit are resolved here; the rest queue for hashing.
  QList<ScannedFile> results;
  results.reserve(total);
  QList<PendingHash> pending;
  for (const QString &path : files) {
    if (cancelled(cancel)) {
      out.cancelled = true;
      break;
    }
    const QFileInfo fi(path);
    const QString canonical = fi.canonicalFilePath();
    const qint64 size = fi.size();
    const qint64 mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    if (cacheDb != nullptr) {
      if (auto hit = FileHashCache::lookup(*cacheDb, canonical, size, mtimeMs)) {
        ScannedFile sf;
        sf.path = path;
        sf.crc = hit->crc;
        sf.md5 = hit->md5;
        sf.sha1 = hit->sha1;
        sf.size = hit->size;
        sf.readOk = true;
        results.append(sf);
        ++done;
        tick(path);
        continue;
      }
    }
    pending.append(PendingHash{path, canonical, size, mtimeMs});
  }

  // Phase 2: hash the misses concurrently on the global pool; consume results
  // in order, writing each back to the cache (serial) as it lands.
  if (!out.cancelled && !pending.isEmpty()) {
    QFuture<ScannedFile> fut = QtConcurrent::mapped(
        pending, [cancel](const PendingHash &p) { return hashToScanned(p.path, cancel); });
    for (int i = 0; i < pending.size(); ++i) {
      if (cancelled(cancel)) {
        fut.cancel();
        fut.waitForFinished();
        out.cancelled = true;
        break;
      }
      const ScannedFile sf = fut.resultAt(i); // blocks until item i is ready
      if (cacheDb != nullptr && sf.readOk) {
        auto saved = FileHashCache::store(*cacheDb, pending.at(i).canonical, sf.size,
                                          pending.at(i).mtimeMs, sf.crc, sf.md5, sf.sha1);
        if (saved.isError()) {
          ErrorUtils::logError(saved.error());
        }
      }
      results.append(sf);
      ++done;
      tick(sf.path);
    }
  }

  AuditOutput classified = classify(catalogue, results);
  out.rows = std::move(classified.rows);
  out.summary = classified.summary;
  // A cancelled run scanned only some files, so totalFiles reflects what was
  // actually hashed, not the full enumeration.
  return out;
}

} // namespace DatAudit
