#ifndef LAUNCHPREVIEW_H
#define LAUNCHPREVIEW_H

// LaunchPreview leaf header (Kartend-rq33v), peeled from launchmanager.h
// following the collection/ leaf-header precedent: ui-layer consumers
// (LaunchPreviewDialog) and interactionmanager.h needed only this struct but
// dragged in the full 700-line manager header.

#include <QString>
#include <QStringList>

/// Read-only summary of what `LaunchManager::launchItem()` would do for a
/// given (collection, launcher, file) triple, without spawning a child
/// process. Drives the launch-preview / dry-run validation UI: each warning
/// surfaces a specific user-fixable issue (missing executable, file gone,
/// archive extraction without a target extension, etc.). `buildOk` is false
/// when buildLaunchCommand itself rejected the input (e.g. missing libretro
/// core); `buildError` carries the message.
struct LaunchPreview {
  /// True iff buildLaunchCommand returned a valid program + arguments.
  /// When false, `program` and `arguments` are empty and `warnings` is the
  /// single-entry list `[buildError]`.
  bool buildOk = false;
  /// Diagnostic message from buildLaunchCommand when buildOk == false.
  QString buildError;
  /// Raw launcher path as configured (may be a bare command name).
  QString program;
  /// Argument list, with %collection% / preset substitutions applied.
  QStringList arguments;
  /// Path returned by validateLauncherPath — empty when the launcher is
  /// not on PATH / doesn't exist on disk.
  QString resolvedProgram;
  /// True when the file at `filePath` exists on disk at preview time.
  bool fileExists = false;
  /// True when the collection's archive-extraction toggle would apply to
  /// this file. UI shows the extracted-extension target alongside.
  bool wouldExtractArchive = false;
  /// The target extension the archive would be unpacked to (set when
  /// wouldExtractArchive is true). Empty string when extraction is on but
  /// no extension was configured — surfaced as a warning.
  QString archiveTargetExtension;
  /// Human-readable warnings — empty list means the command is ready.
  QStringList warnings;
};

#endif // LAUNCHPREVIEW_H
