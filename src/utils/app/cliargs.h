#ifndef CLIARGS_H
#define CLIARGS_H

#include "errorutils.h"
#include <QString>
#include <QStringList>

namespace CliArgs {

enum class KartConflictPolicy { Skip = 0, Overwrite, Merge };

struct StartupOptions {
  // when non-empty, names a collection that overrides the
  // persisted startupCollection setting for this launch. The startup logic
  // falls back to the default selection if the name doesn't match an
  // existing collection.
  QString collectionOverride;

  // headless Kart operations. importKartPath / importDestDir are already
  // ~-expanded and shell-metachar / NUL validated by parseStartupArguments;
  // a validation failure populates pathValidationError and leaves the path
  // unchanged so main.cpp can surface a clear message before doing anything
  // with the input.
  QString importKartPath;
  QString importDestDir;
  QString exportCollectionName;
  QString exportOutPath;
  KartConflictPolicy onConflict = KartConflictPolicy::Skip;

  // Kartend-u8wf0: opt-in to importing a .kart that bundles its own launcher
  // executable. Headless import refuses a self-bundled launcher by default,
  // because there is no interactive confirmer to warn the user. Off unless
  // --allow-untrusted-launcher is passed; main.cpp forwards it to the import.
  bool allowUntrustedLauncher = false;

  // Non-Success when a CLI argument failed CLI-layer validation: an
  // --import-kart / --to path issue (shell metachars, NUL byte,
  // blank-after-expansion) or an unrecognized --on-conflict policy value.
  // main.cpp must reject the run with a non-zero exit on isError() — path
  // existence is NOT checked here; KartReader handles that with its own
  // readable errors.
  ErrorUtils::ErrorContext pathValidationError;
};

[[nodiscard]] StartupOptions parseStartupArguments(const QStringList &arguments);

} // namespace CliArgs

#endif // CLIARGS_H
