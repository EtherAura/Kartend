#include "datauditfix.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTemporaryFile>

#include "archiverepack.h"
#include "romhasher.h"

namespace DatAudit {

namespace {

QString sanitizeFolderName(const QString &name); // defined below; used by planArchiveContainer

/// The archive container an audit row lives in, or empty when the path is a real
/// on-disk file. The runner builds archive members as `<archive>/<entry>`, so the
/// container is the nearest ancestor component bearing an archive extension.
/// Pure string walk — no filesystem access (keeps computeFixPlan pure).
QString archiveContainerOf(const QString &filePath) {
  int slash = filePath.lastIndexOf(QLatin1Char('/'));
  while (slash > 0) {
    const QString ancestor = filePath.left(slash);
    if (RomHasher::isArchivePath(ancestor)) {
      return ancestor;
    }
    slash = ancestor.lastIndexOf(QLatin1Char('/'));
  }
  return {};
}

/// Plan the fix for one archive container from the audit rows whose members live
/// inside it. Repacks a misnamed set (rename inner ROM(s) to canonical + the
/// container to <game>.<ext>); quarantines a container that holds only
/// unknown/wrong-content; skips anything ambiguous (mixed good+bad, or members
/// spanning multiple games) for safety. Single-ROM zips are the common case.
void planArchiveContainer(FixPlan &plan, const QString &container,
                          const QList<const AuditRow *> &members, const FixSettings &settings) {
  bool anyBad = false;    // Unknown / WrongHash
  bool anyGood = false;   // Have / WrongName (catalogued content)
  bool anyRename = false; // WrongName (a member whose name needs fixing)
  QSet<QString> games;
  for (const AuditRow *r : members) {
    switch (r->status) {
    case Status::Unknown:
    case Status::WrongHash:
      anyBad = true;
      break;
    case Status::WrongName:
      anyRename = true;
      [[fallthrough]];
    case Status::Have:
      anyGood = true;
      if (!r->gameName.isEmpty()) {
        games.insert(r->gameName);
      }
      break;
    default:
      break;
    }
  }

  // The whole container is uncatalogued / bad content -> quarantine the container.
  if (anyBad && !anyGood) {
    if (settings.quarantineUnknown && !settings.quarantineDir.isEmpty()) {
      const QString to =
          settings.quarantineDir + QLatin1Char('/') + QFileInfo(container).fileName();
      plan.actions.append(
          FixAction{FixActionKind::Quarantine, container, to, members.first()->status, {}});
    }
    return;
  }
  // Mixed good+bad (repacking can't drop the bad member, quarantining loses the
  // good), or members from more than one game (ambiguous container name): skip.
  if (anyBad || games.size() > 1) {
    return;
  }
  if (!settings.rename || !anyRename) {
    return; // nothing to repack (all members already canonical)
  }

  // Rename each misnamed inner ROM to its canonical name.
  const QString prefix = container + QLatin1Char('/');
  QList<QPair<QString, QString>> inner;
  for (const AuditRow *r : members) {
    if (r->status != Status::WrongName || r->expectedName.isEmpty() ||
        !r->filePath.startsWith(prefix)) {
      continue;
    }
    const QString oldInner = r->filePath.mid(prefix.size());
    if (oldInner != r->expectedName) {
      inner.append({oldInner, r->expectedName});
    }
  }

  // Rename the container itself to <game>.<container-ext>.
  const QString ext = container.mid(container.lastIndexOf(QLatin1Char('.')));
  QString newContainer = container;
  if (!games.isEmpty()) {
    const QString game = sanitizeFolderName(*games.constBegin());
    if (!game.isEmpty()) {
      newContainer = QFileInfo(container).path() + QLatin1Char('/') + game + ext;
    }
  }

  if (inner.isEmpty() && newContainer == container) {
    return; // already fully canonical
  }
  if (inner.isEmpty()) {
    // Only the container name is wrong — a plain file rename, no rewrite needed.
    plan.actions.append(
        FixAction{FixActionKind::Rename, container, newContainer, Status::WrongName, {}});
    return;
  }
  plan.actions.append(
      FixAction{FixActionKind::Repack, container, newContainer, Status::WrongName, inner});
}

/// Make a DAT game name safe to use as a single folder component: replace the
/// characters illegal on Windows/cross-platform filesystems with '_' and trim
/// trailing dots/spaces (also illegal on Windows). Game names are usually
/// already clean ("Title (USA) (Rev 1)"); this only guards the odd ':' etc.
QString sanitizeFolderName(const QString &name) {
  QString out;
  out.reserve(name.size());
  for (const QChar c : name) {
    if (c == u'<' || c == u'>' || c == u':' || c == u'"' || c == u'/' || c == u'\\' || c == u'|' ||
        c == u'?' || c == u'*' || c.unicode() < 0x20) {
      out += u'_';
    } else {
      out += c;
    }
  }
  while (out.endsWith(u'.') || out.endsWith(u' ')) {
    out.chop(1);
  }
  return out;
}

// Repack `src` into `dst` (applying `renames`) via a temp file in dst's own
// directory, then place it atomically: when dst differs from src, move temp->dst
// and remove the old src; when they are the same file (only inner names changed),
// replace in place. A failure never leaves a half-written destination. Returns
// true on success; on failure writes a message to errorOut when non-null.
bool repackAndSwap(const QString &src, const QString &dst,
                   const QList<QPair<QString, QString>> &renames, QString *errorOut) {
  const QString destDir = QFileInfo(dst).absolutePath();
  QTemporaryFile tf(destDir + QStringLiteral("/.kartend-repack-XXXXXX"));
  tf.setAutoRemove(false);
  if (!tf.open()) {
    if (errorOut != nullptr) {
      *errorOut = QStringLiteral("could not create temp file in %1").arg(destDir);
    }
    return false;
  }
  const QString tmp = tf.fileName();
  tf.close();

  QHash<QString, QString> map;
  for (const auto &pr : renames) {
    map.insert(pr.first, pr.second);
  }
  // ArchiveRepack overwrites the temp; it refuses a src==dst, and tmp != src.
  const auto rr = ArchiveRepack::repack(src, tmp, map);
  if (rr.isError()) {
    QFile::remove(tmp);
    if (errorOut != nullptr) {
      *errorOut = rr.error().message;
    }
    return false;
  }

  const bool inPlace = QFileInfo(src).absoluteFilePath() == QFileInfo(dst).absoluteFilePath();
  if (inPlace) {
    QFile::remove(src); // QFile::rename won't overwrite, so clear the slot first
  }
  if (!QFile::rename(tmp, dst)) {
    QFile::remove(tmp);
    if (errorOut != nullptr) {
      *errorOut = QStringLiteral("could not place repacked archive: %1").arg(dst);
    }
    return false;
  }
  if (!inPlace) {
    QFile::remove(src); // drop the old, now-renamed container
  }
  return true;
}

} // namespace

QString fixActionKindToken(FixActionKind k) {
  switch (k) {
  case FixActionKind::Rename:
    return QStringLiteral("rename");
  case FixActionKind::Relocate:
    return QStringLiteral("relocate");
  case FixActionKind::Quarantine:
    return QStringLiteral("quarantine");
  case FixActionKind::Repack:
    return QStringLiteral("repack");
  }
  return QStringLiteral("rename");
}

FixPlan computeFixPlan(const QList<AuditRow> &rows, const FixSettings &settings) {
  FixPlan plan;
  // Archive members carry a virtual path (`<archive>/<entry>`) with no on-disk
  // file — they can't be renamed/moved directly, so collect them per container
  // and fix each archive as a unit (repack / quarantine) after the real-file
  // rows. Stable container order keeps the plan deterministic.
  QHash<QString, QList<const AuditRow *>> byContainer;
  QStringList containerOrder;
  for (const AuditRow &r : rows) {
    if (!r.filePath.isEmpty()) {
      const QString container = archiveContainerOf(r.filePath);
      if (!container.isEmpty()) {
        if (!byContainer.contains(container)) {
          containerOrder.append(container);
        }
        byContainer[container].append(&r);
        continue;
      }
    }
    // Rename in place: content is correct, only the name is wrong.
    if (settings.rename && r.status == Status::WrongName && !r.filePath.isEmpty() &&
        !r.expectedName.isEmpty()) {
      // path() (not absolutePath()): keep the file's own directory string
      // without resolving against the current drive/CWD, so a rename stays in
      // place cross-platform (absolutePath() would prepend e.g. "D:" on Windows).
      const QString dir = QFileInfo(r.filePath).path();
      const QString to = dir + QLatin1Char('/') + r.expectedName;
      if (to != r.filePath) {
        plan.actions.append(FixAction{FixActionKind::Rename, r.filePath, to, r.status});
      }
    }
    // Relocate present entries into the managed-output root (a clean sorted
    // set; originals are left untouched — this is a copy).
    if (settings.relocateToManagedOutput && !settings.managedOutputRoot.isEmpty() &&
        isPresent(r.status) && !r.filePath.isEmpty() && !r.expectedName.isEmpty()) {
      // Structured output: group each file under a per-item subfolder named for
      // its game (Kartend-m6qsb.14); fall back to a flat destination when the
      // row carries no usable game name.
      QString destDir = settings.managedOutputRoot;
      if (settings.managedOutputPerItemSubfolder) {
        const QString folder = sanitizeFolderName(r.gameName);
        if (!folder.isEmpty()) {
          destDir += QLatin1Char('/') + folder;
        }
      }
      const QString to = destDir + QLatin1Char('/') + r.expectedName;
      plan.actions.append(FixAction{FixActionKind::Relocate, r.filePath, to, r.status});
    }
    // Quarantine unknown and wrong-content files (move out, never delete): an
    // Unknown file matches no DAT entry, and a WrongHash file claims (by name) to
    // be a known entry but holds bytes no entry matches — neither belongs in the
    // verified set, so set both aside. classify() routes a content match (even
    // under the wrong name) to WrongName/rename, so WrongHash is genuinely
    // unrecoverable here and safe to move.
    if (settings.quarantineUnknown && !settings.quarantineDir.isEmpty() &&
        (r.status == Status::Unknown || r.status == Status::WrongHash) && !r.filePath.isEmpty()) {
      const QString name = r.actualName.isEmpty() ? QFileInfo(r.filePath).fileName() : r.actualName;
      const QString to = settings.quarantineDir + QLatin1Char('/') + name;
      plan.actions.append(FixAction{FixActionKind::Quarantine, r.filePath, to, r.status});
    }
  }
  // Fix each archive-packed container as a unit (repack / quarantine).
  for (const QString &container : containerOrder) {
    planArchiveContainer(plan, container, byContainer.value(container), settings);
  }
  return plan;
}

ApplyResult applyFixPlan(const FixPlan &plan, bool dryRun) {
  ApplyResult res;
  for (const FixAction &a : plan.actions) {
    if (dryRun) {
      ++res.applied;
      continue;
    }
    if (a.fromPath.isEmpty() || a.toPath.isEmpty() || !QFileInfo::exists(a.fromPath)) {
      ++res.failed;
      res.errors.append(QStringLiteral("source missing: %1").arg(a.fromPath));
      continue;
    }
    // Never overwrite a different existing file at the destination.
    if (QFileInfo::exists(a.toPath) &&
        QFileInfo(a.toPath).canonicalFilePath() != QFileInfo(a.fromPath).canonicalFilePath()) {
      ++res.skipped;
      res.errors.append(QStringLiteral("destination exists, skipped: %1").arg(a.toPath));
      continue;
    }
    const QString parent = QFileInfo(a.toPath).absolutePath();
    if (!parent.isEmpty()) {
      QDir().mkpath(parent);
    }

    bool ok = false;
    if (a.kind == FixActionKind::Repack) {
      QString err;
      ok = repackAndSwap(a.fromPath, a.toPath, a.innerRenames, &err);
      if (ok) {
        UndoEntry u;
        u.wasRepack = true;
        u.createdPath = a.toPath;
        u.originalPath = a.fromPath;
        for (const auto &pr : a.innerRenames) {
          u.repackReverse.append({pr.second, pr.first}); // new -> old
        }
        res.undo.append(u);
      } else if (!err.isEmpty()) {
        res.errors.append(err);
      }
    } else if (a.kind == FixActionKind::Relocate) {
      ok = QFile::copy(a.fromPath, a.toPath);
      if (ok) {
        res.undo.append(UndoEntry{a.toPath, QString(), true});
      }
    } else {
      ok = QFile::rename(a.fromPath, a.toPath);
      if (ok) {
        res.undo.append(UndoEntry{a.toPath, a.fromPath, false});
      }
    }
    if (ok) {
      ++res.applied;
    } else {
      ++res.failed;
      // Repack already recorded a specific error above.
      if (a.kind != FixActionKind::Repack) {
        res.errors.append(QStringLiteral("failed: %1 -> %2").arg(a.fromPath, a.toPath));
      }
    }
  }
  return res;
}

int applyUndo(const QList<UndoEntry> &undo) {
  int reverted = 0;
  // Reverse order so later actions are undone before earlier ones.
  for (int i = undo.size() - 1; i >= 0; --i) {
    const UndoEntry &e = undo.at(i);
    bool ok = false;
    if (e.wasRepack) {
      // Reverse the repack: rewrite the new container back to the original name
      // with the inverse inner renames (same temp-then-swap mechanics as apply).
      ok = repackAndSwap(e.createdPath, e.originalPath, e.repackReverse, nullptr);
    } else if (e.wasCopy) {
      ok = QFile::remove(e.createdPath);
    } else if (!e.originalPath.isEmpty()) {
      ok = QFile::rename(e.createdPath, e.originalPath);
    }
    if (ok) {
      ++reverted;
    }
  }
  return reverted;
}

} // namespace DatAudit
