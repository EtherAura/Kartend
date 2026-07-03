#ifndef LAUNCHCOMMANDBUILDER_H
#define LAUNCHCOMMANDBUILDER_H

// Pure command/parameter-construction half of the launch module, split out of
// launchmanager.cpp along its pure/process seam. Everything here is config in
// -> LaunchCommand/LaunchPreview out: no process is ever spawned and no I/O
// happens beyond read-only path checks (existence/executability probes on the
// preview path). LaunchManager keeps same-signature public statics as thin
// delegations so existing callers and tests are untouched; new code can call
// these free functions directly without dragging in the QObject manager.

#include <QString>
#include <QStringList>

#include "collection/collectionconfig.h"
#include "errorutils.h"
#include "launchpreview.h"

/// Program + argument list ready to hand to QProcess. Defined here (not in
/// launchmanager.h) so command construction stays header-independent of the
/// QObject manager; launchmanager.h includes this header, so existing
/// includers keep seeing the struct unchanged.
struct LaunchCommand {
  QString program;
  QStringList arguments;
};

namespace LaunchCommandBuilder {

/// Parses a launch-parameters string into argv tokens, honouring single/double
/// quotes and backslash escapes. Returns an error for unclosed quotes and for
/// control characters (potential injection vectors).
[[nodiscard]] ErrorUtils::Result<QStringList> parseParameters(const QString &paramString);

/// Builds the program + argument list for a single (preset-resolved) launcher
/// entry. Pure construction: it does NOT verify the launcher exists or is
/// executable (LaunchManager::validateLauncherPath owns that); it only
/// assembles the command and validates argument semantics — %collection% /
/// %1 / %f / %core substitution, the libretro `-L <core> <file>` triple, and
/// the append-media-path fallback when no file placeholder is present.
/// `collectionName` is used solely for diagnostics and `%collection%`
/// substitution.
[[nodiscard]] ErrorUtils::Result<LaunchCommand> buildLaunchCommand(const LauncherConfig &launcher,
                                                                   const QString &collectionName,
                                                                   const QString &filePath);

/// Read-only dry-run: returns a LaunchPreview for the given launcher /
/// collection / file triple without spawning a child process. The launcher is
/// taken pre-resolved (preset resolution is the caller's responsibility) so
/// the preview shows exactly what would be executed. Probes the filesystem
/// read-only (file existence, launcher resolution) to populate the warnings.
[[nodiscard]] LaunchPreview previewLaunchCommand(const CollectionConfig &collection,
                                                 const LauncherConfig &launcher,
                                                 const QString &filePath);

} // namespace LaunchCommandBuilder

#endif // LAUNCHCOMMANDBUILDER_H
