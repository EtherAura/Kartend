#include "datauditfix.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace DatAudit {

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
      const QString to = settings.managedOutputRoot + QLatin1Char('/') + r.expectedName;
      plan.actions.append(FixAction{FixActionKind::Relocate, r.filePath, to, r.status});
    }
    // Quarantine unknown files (move, never delete).
    if (settings.quarantineUnknown && !settings.quarantineDir.isEmpty() &&
        r.status == Status::Unknown && !r.filePath.isEmpty()) {
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
