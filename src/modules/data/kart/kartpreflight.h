#ifndef KARTEND_MODULES_DATA_KART_KARTPREFLIGHT_H
#define KARTEND_MODULES_DATA_KART_KARTPREFLIGHT_H

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

#include "kartlaunchertrust.h"
#include "kartmanifest.h"

/// Pre-import validation pass over a parsed kart manifest. Computes a report
/// the UI can show the user before any state is mutated:
///
///   • Bundle summary — collection name, type, item count, has artwork /
///     metadata flags.
///   • Launcher issues — every launcher path (primary + additional + per-item
///     overrides) is checked against the local filesystem / PATH so the user
///     learns about missing emulators or media players up front instead of
///     watching the first launch fail.
///   • Launcher trust — every launcher field the bundle supplies (paths,
///     cores, launch parameters), because an imported launcher block chooses
///     both the program and its arguments. Disclosure is unconditional; the
///     allowlist only sets each finding's severity. See kartlaunchertrust.h.
///   • Suspicious paths — the icon / placeholder half of the same allowlist,
///     surfaced through the same report so the dialog can show every concern
///     in one place.
///   • Name conflicts — flags the case where a collection with the same name
///     already exists; the merge dialog still handles per-item conflicts
///     later, but the preflight warns about full-collection collisions.
///
/// All inputs are read-only; the helper is pure (no QObject) so it
/// unit-tests cleanly.
namespace KartPreflight {

enum class LauncherCheck {
  Ok,            ///< Path resolves and is executable.
  Missing,       ///< QFileInfo::exists() returned false.
  NotExecutable, ///< Path exists but isExecutable() returned false.
  EmptyPath,     ///< launcherPath is empty (relies on PATH); treated as Ok
                 ///< only when the basename resolves via findExecutable.
};

struct LauncherIssue {
  /// User-facing label, e.g. "Primary launcher", "Additional launcher: VLC",
  /// or "Item override: my_video.mkv".
  QString label;
  QString path;
  LauncherCheck reason = LauncherCheck::Missing;
};

struct PreflightReport {
  // ─── Bundle summary ───────────────────────────────────────
  QString collectionName;
  QString collectionType;
  int itemCount = 0;
  int launcherCount = 0;
  bool hasArtworkOverrides = false;
  bool hasMetadata = false;
  /// Kartend-qbfk1: extraction-policy facts the dialog surfaces so the user
  /// knows what lands on disk before choosing a destination. Set by the
  /// import flow (they need container I/O and the manager's name derivation,
  /// which buildReport — a pure manifest pass — deliberately doesn't do).
  /// bundleEntryCount is -1 when the container walk failed; the name of the
  /// fresh subdirectory everything extracts into is empty when unknown.
  int bundleEntryCount = -1;
  QString extractionSubdirName;

  // ─── Issues to surface ────────────────────────────────────
  QList<LauncherIssue> launcherIssues;
  /// Every launcher field the bundle supplies, whether or not anything looks
  /// wrong with it. Non-empty means the bundle carries executable content, so
  /// the report can never be reported as clean.
  QList<kart::KartLauncherFinding> launcherTrust;
  QList<QPair<QString, QString>> suspiciousPaths; // (field, path) — icon / placeholder
  bool nameConflicts = false;

  /// True when at least one launcher finding tripped a danger signal, as
  /// opposed to merely being supplied by the bundle.
  [[nodiscard]] bool hasElevatedLauncherTrust() const {
    for (const kart::KartLauncherFinding &f : launcherTrust) {
      if (kart::isElevatedLauncherTrustReason(f.reason)) return true;
    }
    return false;
  }

  [[nodiscard]] bool hasIssues() const {
    return !launcherIssues.isEmpty() || !launcherTrust.isEmpty() || !suspiciousPaths.isEmpty() ||
           nameConflicts;
  }
};

/// Check a launcher path. Empty paths return EmptyPath unless the basename
/// resolves via QStandardPaths::findExecutable (the caller can decide how
/// strict to be — EmptyPath isn't always a fatal issue because the
/// user may rely on a PATH-resolved binary).
[[nodiscard]] LauncherCheck classifyLauncherPath(const QString &path);

/// Build the report. @p trustedLauncherPaths lets callers feed the
/// previously-trusted set (matching the existing collectSuspiciousKartPaths
/// allowlist) so re-imports don't re-flag the same paths. @p existingCollectionNames
/// is the lowercase set of collection names already loaded; a hit promotes
/// nameConflicts to true.
[[nodiscard]] PreflightReport buildReport(const KartManifest::Manifest &manifest,
                                          const QSet<QString> &trustedLauncherPaths,
                                          const QSet<QString> &existingCollectionNames);

/// Short human-readable label for a LauncherCheck. Used by the dialog and
/// log lines.
[[nodiscard]] QString launcherCheckLabel(LauncherCheck check);

} // namespace KartPreflight

#endif // KARTEND_MODULES_DATA_KART_KARTPREFLIGHT_H
