#ifndef DATAUDITFIX_H
#define DATAUDITFIX_H

#include <QList>
#include <QString>
#include <QStringList>

#include "dataudittypes.h"

/// The fix engine: turn audit results into a previewable plan, apply it with
/// safety rails, and undo it. Embodies the consensus "Scan -> Plan -> Apply"
/// architecture — computeFixPlan() is pure so the UI can show every before/
/// after before anything on disk changes, and destructive categories default
/// OFF.
namespace DatAudit {

enum class FixActionKind {
  Rename,     ///< Rename a file in place to its canonical DAT name (WrongName).
  Relocate,   ///< Copy a present file into the managed-output root under its canonical name.
  Quarantine, ///< Move an unknown file into the quarantine area (never deleted).
};

[[nodiscard]] QString fixActionKindToken(FixActionKind k);

/// One planned filesystem change.
struct FixAction {
  FixActionKind kind = FixActionKind::Rename;
  QString fromPath;                   ///< The existing file the action operates on.
  QString toPath;                     ///< Destination path.
  Status sourceStatus = Status::Have; ///< The audit status that produced this action.
};

struct FixPlan {
  QList<FixAction> actions;
  [[nodiscard]] bool isEmpty() const { return actions.isEmpty(); }
};

/// Which fixes to include and where outputs go. Destructive categories default
/// OFF; rename (the lowest-risk fix) defaults ON.
struct FixSettings {
  bool rename = true;                   ///< In-place: WrongName -> rename to canonical.
  bool relocateToManagedOutput = false; ///< Managed-output: copy present entries into the root.
  bool quarantineUnknown = false;       ///< Move Unknown files into quarantineDir.
  QString managedOutputRoot;            ///< Required when relocateToManagedOutput.
  QString quarantineDir;                ///< Required when quarantineUnknown.
};

/// Compute the previewable fix plan from audit rows. Pure — no filesystem touch
/// (paths are parsed, not stat'd) — so the UI shows every action before
/// anything mutates. Skips no-op renames (name already canonical), actions with
/// empty source/destination, and categories whose required directory is unset.
[[nodiscard]] FixPlan computeFixPlan(const QList<AuditRow> &rows, const FixSettings &settings);

/// One reversible step recorded by applyFixPlan.
struct UndoEntry {
  QString createdPath;  ///< Path the action created (rename/quarantine target, or the copy).
  QString originalPath; ///< Where to move it back; empty for copies (undo deletes createdPath).
  bool wasCopy = false; ///< Copy (Relocate) vs move (Rename / Quarantine).
};

struct ApplyResult {
  int applied = 0;
  int skipped = 0; ///< Destination already existed; not overwritten.
  int failed = 0;
  QStringList errors;
  QList<UndoEntry> undo; ///< Reverse with applyUndo(); empty on dryRun.
};

/// Execute (or, when `dryRun`, only count) a fix plan with safety rails: parent
/// dirs are created; an existing, different destination is skipped (never
/// overwritten); moves and copies are recorded for undo; quarantine moves never
/// delete. Returns per-action tallies plus the undo log.
[[nodiscard]] ApplyResult applyFixPlan(const FixPlan &plan, bool dryRun);

/// Reverse a prior apply (in reverse order): copies are deleted, moves are
/// moved back. Best effort; returns the count successfully reverted.
int applyUndo(const QList<UndoEntry> &undo);

} // namespace DatAudit

#endif // DATAUDITFIX_H
