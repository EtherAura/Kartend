#include "datauditrunner.h"

#include <algorithm>

#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent/QtConcurrentMap>

#include "datauditregion.h"
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

// Enumerate regular files under `roots` — recursively, unless the user
// confirmed a Flat layout (then subfolders are noise by definition,
// Kartend-m6qsb.6) — skipping basenames that match an ignore glob and any
// path that is one of the audited DAT files.
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
    const QDirIterator::IteratorFlags flags =
        opts.layout == Layout::Flat ? QDirIterator::NoIteratorFlags : QDirIterator::Subdirectories;
    QDirIterator it(root, QDir::Files | QDir::NoSymLinks, flags);
    while (it.hasNext()) {
      const QString path = it.next();
      const QFileInfo fi = it.fileInfo();
      if (matchesAnyGlob(fi.fileName(), globs)) {
        continue;
      }
      // Skip the fix engine's transient sidecars (a crash-orphaned in-place
      // repack backup or temp) so they are never audited as loose ROMs.
      if (fi.fileName().endsWith(QStringLiteral(".kartend-bak")) ||
          fi.fileName().startsWith(QStringLiteral(".kartend-repack"))) {
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
    // Attribute every record from this DAT to a source id so per-DAT
    // completeness can be reported (Kartend-m6qsb.15). The filename is the
    // provenance label the UI shows; the full path is the disambiguator only
    // when two DATs share a basename, which is rare enough not to special-case.
    const int sourceId = cat.addSource(QFileInfo(dat).fileName());
    cache.forEachRecord(src.value(), [&cat, sourceId](const DatLookup::DatRecord &r) {
      cat.addRecord(r, sourceId);
    });
  }
  return cat;
}

AuditOutput classify(const Catalogue &catalogue, const QList<ScannedFile> &files,
                     const QStringList &regionPrefs, bool onePerGame, MergeMode mergeMode) {
  AuditOutput out;
  QSet<int> satisfied;
  out.rows.reserve(files.size() + catalogue.size());

  // Under Merged, a clone record's rows fold under its ROOT parent set so the
  // report groups them there (transitive — a clone-of-a-clone folds all the way
  // up, not one hop); in Split/NonMerged a record keeps its own gameName
  // (Kartend-m6qsb.29). No-op for clone-less catalogues.
  const auto attributedGame = [&](int idx) -> QString {
    if (mergeMode == MergeMode::Merged && catalogue.isClone(idx)) {
      return catalogue.rootSetGameName(idx);
    }
    return catalogue.record(idx).gameName;
  };

  // NonMerged folder-scoped satisfaction (Kartend-m6qsb.29): a non-merged clone
  // set is expected to physically self-contain its inherited roms IN ITS OWN
  // folder/archive, so track which catalogue indices' content is present in each
  // containing folder (the file's immediate directory — the set folder for
  // folder-per-item, the container for archive-per-item). Only populated/used
  // for NonMerged; left empty otherwise.
  QHash<QString, QSet<int>> folderSatisfied; // folder -> catalogue indices present there
  QHash<int, QSet<QString>> idxFolders;      // catalogue index -> folders it is present in

  for (const ScannedFile &f : files) {
    AuditRow row;
    row.filePath = f.path;
    row.actualName = QFileInfo(f.path).fileName();
    row.size = f.size;
    row.crc = f.crc;
    row.md5 = f.md5;
    row.sha1 = f.sha1;
    row.zipIndex = f.zipIndex; // archive member index, -1 for whole-file rows

    if (!f.readOk) {
      row.status = Status::Corrupt;
      out.rows.append(row);
      continue;
    }

    const int idx = catalogue.matchByHash(f.crc, f.md5, f.sha1);
    if (idx >= 0) {
      // The content is physically present in this file's folder — recorded for
      // NonMerged folder-scoped inherited-rom checks (folder = immediate dir, or
      // the archive container for a virtual "container/member" path).
      if (mergeMode == MergeMode::NonMerged) {
        const QString folder = QFileInfo(f.path).path();
        folderSatisfied[folder].insert(idx);
        idxFolders[idx].insert(folder);
      }
      const DatLookup::DatRecord &rec = catalogue.record(idx);
      row.gameName = attributedGame(idx);
      row.expectedName = rec.romName;
      row.sourceName = catalogue.sourceName(catalogue.recordSource(idx));
      row.mia = rec.mia;
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
        row.gameName = attributedGame(nameIdx);
        row.expectedName = rec.romName;
        row.sourceName = catalogue.sourceName(catalogue.recordSource(nameIdx));
        row.mia = rec.mia;
        row.status = Status::WrongHash;
      } else {
        row.status = Status::Unknown;
      }
    }
    out.rows.append(row);
  }

  // Emit a Missing row for catalogue entry `i`. gameOverride attributes the row
  // to a different game than the record's own (the NonMerged pass uses it to
  // report a parent rom under each clone that inherits it); by default the row
  // carries attributedGame(i) (which folds clones under parents in Merged).
  const auto emitMissing = [&](int i, const QString &gameOverride = QString()) {
    const DatLookup::DatRecord &rec = catalogue.record(i);
    AuditRow row;
    row.status = Status::Missing;
    row.gameName = gameOverride.isEmpty() ? attributedGame(i) : gameOverride;
    row.expectedName = rec.romName;
    row.size = rec.size;
    row.crc = rec.crc;
    row.md5 = rec.md5;
    row.sha1 = rec.sha1;
    row.sourceName = catalogue.sourceName(catalogue.recordSource(i));
    row.mia = rec.mia;
    out.rows.append(row);
  };

  if (onePerGame) {
    // 1G1R completeness: group catalogue entries by base game name. A game is
    // covered when ANY of its region variants is present, so only a wholly
    // absent game is Missing — reported once, for the most-preferred region the
    // DAT offers (regionPrefs order; an untagged/unlisted variant ranks last).
    QHash<QString, QList<int>> games;
    QStringList order; // first-seen order, for deterministic output
    for (int i = 0; i < catalogue.size(); ++i) {
      QString key = Region::groupKey(catalogue.record(i).gameName);
      if (key.isEmpty()) {
        key = QChar(0x1F) + QString::number(i); // ungroupable: keep it distinct
      }
      auto it = games.find(key);
      if (it == games.end()) {
        order.append(key);
        games.insert(key, {i});
      } else {
        it.value().append(i);
      }
    }
    for (const QString &key : order) {
      const QList<int> &idxs = games.value(key);
      const bool anyPresent =
          std::any_of(idxs.cbegin(), idxs.cend(), [&](int i) { return satisfied.contains(i); });
      if (anyPresent) {
        continue; // some variant's content is present — game covered under 1G1R
      }
      int wanted = idxs.first();
      int bestRank =
          Region::regionRank(Region::detectRegion(catalogue.record(wanted).gameName), regionPrefs);
      for (int i : idxs) {
        const int rank =
            Region::regionRank(Region::detectRegion(catalogue.record(i).gameName), regionPrefs);
        if (rank < bestRank) {
          bestRank = rank;
          wanted = i;
        }
      }
      emitMissing(wanted);
    }
  } else {
    // Every catalogue entry no file satisfied (by content) is Missing.
    for (int i = 0; i < catalogue.size(); ++i) {
      if (!satisfied.contains(i)) {
        emitMissing(i);
      }
    }
  }

  if (mergeMode == MergeMode::NonMerged && !onePerGame) {
    // A non-merged clone set is expected to physically self-contain its whole
    // inherited-rom chain. For each clone set present on disk, emit a Missing
    // (attributed to the clone) for any ancestor rom whose content is absent
    // from the set's OWN folder(s) — folder-scoped, so the parent's copy living
    // elsewhere in the collection does NOT satisfy it. A set may occupy more than
    // one folder (nested archive members, multi-subdir sets are one logical set),
    // so the rom counts as present if it appears in ANY of them. Presence is by
    // CONTENT (the canonical index for the rom's hashes), so a clone's own copy
    // of a shared rom satisfies it regardless of which record won the hash race.
    // Suppressed under 1G1R, which collapses to one row per absent game
    // (Kartend-m6qsb.29).
    QSet<QString> processedSets; // each clone set discovered once
    QSet<QString> emitted;       // cloneKey + content-key — emit each inherited rom once per set
    for (int i = 0; i < catalogue.size(); ++i) {
      if (!catalogue.isClone(i)) {
        continue;
      }
      const DatLookup::DatRecord &rec = catalogue.record(i);
      const QString cloneKey = rec.setId.isEmpty() ? rec.gameName : rec.setId;
      if (processedSets.contains(cloneKey)) {
        continue;
      }
      processedSets.insert(cloneKey);

      // Folders this clone set physically occupies = where any of its OWN roms'
      // CONTENT is present (keyed by canonical index, so a rom that collides on
      // hashes with an ancestor still maps to the right folder). A set absent on
      // disk has no folder — its own roms already surface as Missing above, so
      // skip the inherited expansion.
      QSet<QString> cloneFolders;
      for (int c : catalogue.recordsForSet(cloneKey)) {
        const DatLookup::DatRecord &crec = catalogue.record(c);
        const int canonicalC = catalogue.matchByHash(crec.crc, crec.md5, crec.sha1);
        if (canonicalC < 0) {
          continue;
        }
        if (auto it = idxFolders.constFind(canonicalC); it != idxFolders.constEnd()) {
          cloneFolders.unite(it.value());
        }
      }
      if (cloneFolders.isEmpty()) {
        continue;
      }

      for (int p : catalogue.ancestorRecords(i)) {
        const DatLookup::DatRecord &prec = catalogue.record(p);
        const int canonical = catalogue.matchByHash(prec.crc, prec.md5, prec.sha1);
        // Present if the content appears in ANY of the set's folders (it self-
        // contains the rom). A hashless ancestor rom (canonical < 0) can't be
        // verified by content, so it is treated as missing.
        bool presentInClone = false;
        if (canonical >= 0) {
          for (const QString &folder : cloneFolders) {
            if (folderSatisfied.value(folder).contains(canonical)) {
              presentInClone = true;
              break;
            }
          }
        }
        if (presentInClone) {
          continue;
        }
        // Dedup once per (set, content); fold the record index into the hashless
        // fallback so two distinct hashless ancestor roms can't collide on name.
        const QString contentKey = canonical >= 0 ? QString::number(canonical)
                                                  : QStringLiteral("n:") + QString::number(p) +
                                                        QLatin1Char(':') + prec.romName;
        const QString dedup = cloneKey + QChar(0x1F) + contentKey;
        if (emitted.contains(dedup)) {
          continue;
        }
        emitted.insert(dedup);
        emitMissing(p, rec.gameName);
      }
    }
  }

  out.summary = summarize(out.rows);
  out.summary.totalCatalogue = catalogue.size();
  out.summary.totalFiles = static_cast<int>(files.size());

  // Per-DAT completeness (Kartend-m6qsb.15): every catalogue entry counts
  // toward its source, present when its content was matched. Deliberately
  // computed on the raw entry set, not the (possibly 1G1R-collapsed) Missing
  // rows, so "of DAT X's N entries, M are present" stays well-defined.
  if (catalogue.sourceCount() > 0) {
    out.summary.perSource.reserve(catalogue.sourceCount());
    for (int s = 0; s < catalogue.sourceCount(); ++s) {
      out.summary.perSource.append(SourceCompleteness{catalogue.sourceName(s), 0, 0, 0});
    }
    for (int i = 0; i < catalogue.size(); ++i) {
      const int s = catalogue.recordSource(i);
      if (s < 0 || s >= out.summary.perSource.size()) {
        continue;
      }
      SourceCompleteness &sc = out.summary.perSource[s];
      sc.total += 1;
      if (satisfied.contains(i)) {
        sc.present += 1;
      }
    }
    for (SourceCompleteness &sc : out.summary.perSource) {
      sc.missing = sc.total - sc.present;
    }
  }
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
  // An archive's audit units are its MEMBERS (Kartend-m6qsb.7) — done for every
  // archive, not just a confirmed archive-per-item layout, so compressed ROMs
  // match the DAT in any layout: a member-cache hit expands here, a miss queues
  // the container for the serial extraction phase below.
  QList<ScannedFile> results;
  results.reserve(total);
  QList<PendingHash> pending;
  QList<PendingHash> pendingArchives;
  for (const QString &path : files) {
    if (cancelled(cancel)) {
      out.cancelled = true;
      break;
    }
    const QFileInfo fi(path);
    const QString canonical = fi.canonicalFilePath();
    const qint64 size = fi.size();
    const qint64 mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    // A DAT indexes the ROM *inside* an archive (its members' hashes), never
    // the .zip/.7z itself — so any archive's audit units are its members,
    // regardless of the detected layout. Whole-file hashing an archive can
    // never match a catalogue entry; gating member extraction on a confirmed
    // archive-per-item layout is what made compressed ROMs invisible
    // (Kartend-34lab follow-up). Look inside every archive we enumerate.
    if (RomHasher::isArchivePath(path)) {
      if (cacheDb != nullptr) {
        if (auto hit = FileHashCache::lookupMembers(*cacheDb, canonical, size, mtimeMs)) {
          for (const FileHashCache::MemberEntry &m : *hit) {
            ScannedFile sf;
            // Virtual member path: container + '/' + member, so the result
            // table shows provenance and basename matching sees the member's
            // own filename. Never a real on-disk path — computeFixPlan excludes
            // it from every fix action (isArchiveMemberPath: an archive-extension
            // ancestor component, datauditfix.cpp).
            sf.path = path + QLatin1Char('/') + m.memberPath;
            sf.crc = m.crc;
            sf.md5 = m.md5;
            sf.sha1 = m.sha1;
            sf.size = m.size;
            sf.readOk = m.size >= 0;
            // Take the index from the cache, not the loop position —
            // lookupMembers sorts by member_path, not archive order
            // (Kartend-7iqhl.4).
            sf.zipIndex = m.zipIndex;
            results.append(sf);
          }
          ++done;
          tick(path);
          continue;
        }
      }
      pendingArchives.append(PendingHash{path, canonical, size, mtimeMs});
      continue;
    }
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
    // Wind the fan-out down before the future leaves scope. On the cancel path
    // this is load-bearing: cancel() then waitForFinished() stops pooled
    // workers from hashing on after the user aborted. On the normal path it's
    // a deterministic-teardown barrier (all hashing truly done before run()
    // returns). NB: this drain does not (and can't) silence the Qt-internal
    // QThreadPool/QtConcurrent teardown races TSan reports for this path — those
    // are stripped-frame Qt false positives with no Kartend data on the racing
    // stacks, so test_datauditrunner skips its run()-driving cases under TSan
    // rather than chase a moving target of suppressions (Kartend-x9mkif.1).
    if (out.cancelled) {
      fut.cancel();
    }
    fut.waitForFinished();
  }

  // Phase 3: archive members (archive-per-item layout). ONE archive at a
  // time — extraction is QProcess + temp-disk bound, and fanning it out like
  // the hash pool above would make the extractions fight each other for the
  // same disk. The per-file hashing inside each archive is already the cheap
  // part once the bytes are on local disk.
  if (!out.cancelled) {
    for (const PendingHash &a : pendingArchives) {
      if (cancelled(cancel)) {
        out.cancelled = true;
        break;
      }
      auto members = RomHasher::hashArchiveMembers(a.path, cancel);
      if (members.isError()) {
        if (members.hasErrorCode(ErrorUtils::ErrorCode::OperationCancelled)) {
          out.cancelled = true;
          break;
        }
        // Unextractable / empty archive: one Corrupt row for the container
        // keeps the problem visible instead of silently skipping it.
        ScannedFile sf;
        sf.path = a.path;
        sf.readOk = false;
        results.append(sf);
      } else {
        QList<FileHashCache::MemberEntry> entries;
        const QList<RomHasher::MemberResult> &mems = members.value();
        entries.reserve(mems.size());
        for (int mi = 0; mi < mems.size(); ++mi) {
          const RomHasher::MemberResult &m = mems.at(mi);
          ScannedFile sf;
          sf.path = a.path + QLatin1Char('/') + m.memberPath;
          sf.crc = m.hashes.crc;
          sf.md5 = m.hashes.md5;
          sf.sha1 = m.hashes.sha1;
          sf.size = m.hashes.size;
          sf.readOk = m.hashes.size >= 0;
          // The member list is the regular-file members in central-directory
          // order (libarchive backend), so its position IS the ZipIndex
          // (Kartend-7iqhl.4); persist it in the cache so a later hit recovers
          // it despite lookupMembers' path sort.
          sf.zipIndex = mi;
          results.append(sf);
          FileHashCache::MemberEntry e;
          e.memberPath = m.memberPath;
          e.crc = m.hashes.crc;
          e.md5 = m.hashes.md5;
          e.sha1 = m.hashes.sha1;
          e.size = m.hashes.size;
          e.zipIndex = mi;
          entries.append(e);
        }
        if (cacheDb != nullptr) {
          auto saved =
              FileHashCache::storeMembers(*cacheDb, a.canonical, a.size, a.mtimeMs, entries);
          if (saved.isError()) {
            ErrorUtils::logError(saved.error());
          }
        }
      }
      ++done;
      tick(a.path);
    }
  }

  AuditOutput classified =
      classify(catalogue, results, opts.regionPrefs, opts.onePerGame, opts.mergeMode);
  out.rows = std::move(classified.rows);
  out.summary = classified.summary;
  // A cancelled run scanned only some files, so totalFiles reflects what was
  // actually hashed, not the full enumeration.
  return out;
}

} // namespace DatAudit
