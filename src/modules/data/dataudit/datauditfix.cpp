#include "datauditfix.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "romhasher.h"

namespace DatAudit {

namespace {

/// True when filePath points INSIDE an archive — the runner builds member rows
/// as `<archive>/<entry>`, a virtual path with no real on-disk file (see
/// datauditrunner.cpp). Such rows can never be renamed/relocated/quarantined, so
/// they must be excluded from every fix action. Walks the ancestor path
/// components string-only (no filesystem access — computeFixPlan stays pure)
/// looking for one that bears an archive extension.
bool isArchiveMemberPath(const QString &filePath) {
  int slash = filePath.lastIndexOf(QLatin1Char('/'));
  while (slash > 0) {
    const QString ancestor = filePath.left(slash);
    if (RomHasher::isArchivePath(ancestor)) {
      return true;
    }
    slash = ancestor.lastIndexOf(QLatin1Char('/'));
  }
  return false;
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

} // namespace

QString fixActionKindToken(FixActionKind k) {
  switch (k) {
  case FixActionKind::Rename:
    return QStringLiteral("rename");
  case FixActionKind::Relocate:
    return QStringLiteral("relocate");
  case FixActionKind::Quarantine:
    return QStringLiteral("quarantine");
  }
  return QStringLiteral("rename");
}

FixPlan computeFixPlan(const QList<AuditRow> &rows, const FixSettings &settings) {
  FixPlan plan;
  for (const AuditRow &r : rows) {
    // Fix actions operate on real on-disk files only. An archive member carries
    // a virtual path (`<archive>/<entry>`) with no on-disk presence, so it can
    // never be renamed/relocated/quarantined — skip the whole row. This is the
    // invariant datauditrunner.cpp documents, enforced here for all three gates.
    if (isArchiveMemberPath(r.filePath)) {
      continue;
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
    if (a.kind == FixActionKind::Relocate) {
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
      res.errors.append(QStringLiteral("failed: %1 -> %2").arg(a.fromPath, a.toPath));
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
    if (e.wasCopy) {
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
