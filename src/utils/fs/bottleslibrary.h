#ifndef KARTEND_UTILS_FS_BOTTLESLIBRARY_H
#define KARTEND_UTILS_FS_BOTTLESLIBRARY_H

#include <QList>
#include <QString>
#include <QStringList>

/// Read-only discovery of the Windows programs a local Bottles install runs —
/// the `External_Programs` a user has added to each bottle, read from that
/// bottle's own `bottle.yml` (Kartend-4cff2). Nothing is executed: the
/// `bottles-cli` invocation only happens later, at launch.
///
/// THE YAML PROBLEM: Bottles persists its config with PyYAML's SafeDumper, and
/// Kartend has no YAML dependency. Rather than take one on for a single
/// source, this reader parses the narrow shape that dumper emits — block-style
/// mappings, space indentation, quoted or plain scalars — and only the handful
/// of keys it needs. Anything it does not recognise (flow mappings, block
/// scalars, aliases) makes it skip that entry rather than guess. See
/// parseConfig().
namespace BottlesLibrary {

struct Program {
  QString bottle;     ///< Bottle display name — `bottles-cli run -b`.
  QString name;       ///< Program display name — `bottles-cli run -p`.
  QString executable; ///< Windows executable file name; informational.
  QString iconPath;   ///< Absolute path to a real icon file, when one is set.
};

struct Bottle {
  QString name;
  QList<Program> programs;
};

/// First existing Bottles data dir: $XDG_DATA_HOME/bottles (or
/// ~/.local/share/bottles), then the Flatpak app-data location. Empty when
/// Bottles isn't found (no `bottles/` subdirectory).
[[nodiscard]] QString defaultDataDir();

/// Every bottle under `<dataDir>/bottles/`, each with the external programs
/// its config lists, sorted by bottle then program name. A bottle whose config
/// is missing or unparseable is skipped.
[[nodiscard]] QList<Bottle> bottles(const QString &dataDir);

/// Flattened `bottles()` — every program across every bottle, sorted by name.
[[nodiscard]] QList<Program> installedPrograms(const QString &dataDir);

/// The fields this reader needs from one `bottle.yml`. Exposed for tests.
/// Returns an empty name when the file is missing or its shape is not the one
/// documented above.
[[nodiscard]] Bottle parseConfig(const QString &configPath);

/// The part of the Bottles invocation a launcher template cannot express.
///
/// The full command is `bottles-cli run -p <program> -b <bottle> --`, and a
/// collection template can only substitute ONE value — the program, as
/// `run -p %1`. This returns what has to ride along on the stub instead:
/// `{"-b", <bottle>, "--"}`. Kept next to the reader so the stub writer and
/// the collection template stay describable in one place.
[[nodiscard]] QStringList stubLaunchArguments(const Program &program);

} // namespace BottlesLibrary

#endif
