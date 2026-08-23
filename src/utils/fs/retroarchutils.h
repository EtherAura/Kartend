#ifndef RETROARCHUTILS_H
#define RETROARCHUTILS_H

#include <QList>
#include <QString>
#include <QStringList>

/// Helpers for discovering what a local RetroArch installation holds —
/// its libretro cores (so the UI can offer a pick-from-list instead of
/// forcing the user to browse to a `.so` / `.dll` / `.dylib` by hand) and
/// its assets directory (the on-disk icon packs, see retroarchicons.h).
///
/// RetroArch keeps both directories in `retroarch.cfg`, under the
/// `libretro_directory` and `assets_directory` keys. This module locates
/// that config (standard per-OS paths, or a user override) and reads the
/// key it needs.
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

/// Read the `assets_directory` value from a `retroarch.cfg`. Same
/// contract as coreDirectoryFromConfig — empty when the file can't be
/// read, the key is absent, or it holds RetroArch's `"default"`
/// sentinel.
///
/// Falls back to a sibling `assets/` directory of the config itself when
/// the key is missing: RetroArch only writes `assets_directory` once the
/// path has been resolved, and a config that predates that still sits
/// beside the assets it uses.
[[nodiscard]] QString assetsDirectoryFromConfig(const QString &configPath);

/// Resolve the assets directory, with the same override semantics as
/// resolveCoreDirectory:
///   * `override` is a directory  → used as-is when it looks like an
///                                  assets tree, else its `retroarch.cfg`
///                                  is read if it holds one.
///   * `override` is a .cfg file  → its `assets_directory` is read.
///   * `override` is empty        → the standard config paths are probed.
/// Returns an empty string when nothing usable is found — the caller
/// treats that as "RetroArch is not installed here".
///
/// Takes the SAME override value as resolveCoreDirectory (the global
/// `launchers.retroarchConfigPath` setting), so a user who has already
/// pointed Kartend at a non-standard install does not point at it twice.
[[nodiscard]] QString resolveAssetsDirectory(const QString &overridePath = QString());

} // namespace RetroArchUtils

#endif // RETROARCHUTILS_H
