#ifndef RETROARCHUTILS_H
#define RETROARCHUTILS_H

#include <QList>
#include <QString>
#include <QStringList>

/// Helpers for discovering a RetroArch installation's libretro cores so
/// the UI can offer a pick-from-list instead of forcing the user to
/// browse to a `.so` / `.dll` / `.dylib` by hand.
///
/// RetroArch keeps the core directory in `retroarch.cfg` under the
/// `libretro_directory` key. This module locates that config (standard
/// per-OS paths, or a user override), reads the key, and enumerates the
/// cores in the resulting directory.
namespace RetroArchUtils {

/// One discovered libretro core.
struct Core {
  /// Absolute path to the core file.
  QString path;
  /// Short, human-friendly name — the file name with the trailing
  /// `_libretro` tag and the extension stripped (e.g. `snes9x`).
  QString displayName;
};

/// Standard `retroarch.cfg` locations for the current OS, in priority
/// order. Existence is NOT checked here — callers probe each in turn.
[[nodiscard]] QStringList defaultConfigPaths();

/// Read the `libretro_directory` value from a `retroarch.cfg`. Returns
/// an empty string when the file can't be read or the key is absent /
/// set to RetroArch's `"default"` sentinel.
[[nodiscard]] QString coreDirectoryFromConfig(const QString &configPath);

/// Resolve the libretro core directory.
///   * `override` is a directory  → used as-is.
///   * `override` is a .cfg file  → its `libretro_directory` is read.
///   * `override` is empty        → the standard config paths are
///                                  probed until one resolves.
/// Returns an empty string when nothing usable is found.
[[nodiscard]] QString resolveCoreDirectory(const QString &overridePath = QString());

/// Enumerate the libretro cores (`.so` / `.dll` / `.dylib`) in
/// `coreDirectory`, sorted by display name. Empty when the directory
/// is missing or holds no cores.
[[nodiscard]] QList<Core> discoverCores(const QString &coreDirectory);

} // namespace RetroArchUtils

#endif // RETROARCHUTILS_H
