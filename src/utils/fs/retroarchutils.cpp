// Discovery of a RetroArch installation's libretro cores. See the
// header for the resolution order; the cfg parse is a deliberately
// small line scanner — retroarch.cfg is a flat `key = "value"` file.
#include "retroarchutils.h"

#include "pathutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

#include <algorithm>

namespace RetroArchUtils {

namespace {

// Bounds for the GUI-thread discovery work (Kartend-m8jfc). Both run inline
// on the main thread when the launcher editor / launcher settings tab opens,
// so a pathological (or attacker-supplied) retroarch.cfg / core directory must
// not drive unbounded work.
//
// Cap the cfg read so a giant or malformed config can't drive unbounded line
// processing before the first `libretro_directory` line is found. A real
// retroarch.cfg holds a few hundred short keys; these ceilings are generous.
constexpr int kMaxConfigLinesScanned = 100000;
constexpr qint64 kMaxConfigBytesScanned = 16 * 1024 * 1024; // 16 MiB
// Cap the number of core-directory entries processed, mirroring the existing
// UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED ceiling (50000) used by
// the archive-extraction scan. A normal core dir holds well under a thousand
// entries; this bounds a pathological directory to a finite result set.
constexpr int kMaxCoresInspected = 50000;

// Libretro core file extensions across the three desktop platforms.
const QStringList &coreExtensions() {
  static const QStringList kExts = {QStringLiteral("so"), QStringLiteral("dll"),
                                    QStringLiteral("dylib")};
  return kExts;
}

// retroarch.cfg routinely stores paths with a leading `~` for the home
// directory; QDir / QFileInfo do NOT expand it, so a `~`-rooted core
// directory would look non-existent. Delegate to the canonical PathUtils
// expansion (only "~/" and a bare "~" expand) so the rules can't drift.
QString expandHome(const QString &path) {
  return PathUtils::expandPathWithoutExistenceCheck(path);
}

} // namespace

QStringList defaultConfigPaths() {
  QStringList paths;
  const QString home = QDir::homePath();

  // QStandardPaths' config location already honours $XDG_CONFIG_HOME /
  // the per-OS equivalent; RetroArch nests its config under a
  // `retroarch/` subdirectory of it.
  const QStringList configDirs =
      QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
  for (const QString &dir : configDirs) {
    paths << QDir(dir).filePath(QStringLiteral("retroarch/retroarch.cfg"));
  }

#if defined(Q_OS_LINUX)
  // Flatpak keeps a fully separate config tree.
  paths << QDir(home).filePath(
      QStringLiteral(".var/app/org.libretro.RetroArch/config/retroarch/retroarch.cfg"));
  paths << QDir(home).filePath(QStringLiteral(".config/retroarch/retroarch.cfg"));
#elif defined(Q_OS_MACOS)
  paths << QDir(home).filePath(
      QStringLiteral("Library/Application Support/RetroArch/config/retroarch.cfg"));
#elif defined(Q_OS_WIN)
  const QByteArray appData = qgetenv("APPDATA");
  if (!appData.isEmpty()) {
    paths << QDir(QString::fromLocal8Bit(appData))
                 .filePath(QStringLiteral("RetroArch/retroarch.cfg"));
  }
#endif

  paths.removeDuplicates();
  return paths;
}

namespace {

// Shared line scanner behind coreDirectoryFromConfig / assetsDirectoryFromConfig.
// retroarch.cfg is a flat `key = "value"` file and every directory key wants
// identical treatment — quote stripping, the "default" sentinel, `~` and
// relative-path expansion, and the security validation. Parameterising the key
// keeps those rules in ONE place; the two public wrappers differ only in the
// key they ask for and in what they do when it is absent.
QString directoryFromConfig(const QString &configPath, QLatin1StringView key) {
  QFile file(configPath);
  if (configPath.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  QTextStream stream(&file);
  // Kartend-m8jfc: bound the read in addition to stopping at the first
  // matching line. A giant or malformed config (synced dotfiles, a
  // shipped image, an attacker-supplied path override) must not drive unbounded
  // line processing on the GUI thread before the target key is reached. Give up
  // (treat as unset) once either ceiling is hit; discovery falls through to the
  // standard probe in resolveCoreDirectory / resolveAssetsDirectory.
  int linesScanned = 0;
  qint64 bytesScanned = 0;
  while (!stream.atEnd()) {
    const QString rawLine = stream.readLine();
    ++linesScanned;
    bytesScanned += rawLine.size();
    if (linesScanned > kMaxConfigLinesScanned || bytesScanned > kMaxConfigBytesScanned) {
      return {};
    }
    const QString line = rawLine.trimmed();
    if (!line.startsWith(key)) {
      continue;
    }
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq < 0) {
      continue;
    }
    // The key must be the WHOLE left-hand side, not merely its prefix.
    // retroarch.cfg holds keys that extend one another — `assets_directory`
    // sits beside `core_assets_directory` and `bundle_assets_*` — so matching
    // on the prefix alone could hand back a neighbouring key's path.
    if (QStringView(line).left(eq).trimmed() != key) {
      continue;
    }
    QString value = line.mid(eq + 1).trimmed();
    // retroarch.cfg quotes string values; strip a matched pair.
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) &&
        value.endsWith(QLatin1Char('"'))) {
      value = value.mid(1, value.size() - 2);
    }
    // RetroArch writes "default" (or leaves it blank) when the
    // directory hasn't been customised — not a real path.
    if (value.isEmpty() || value == QStringLiteral("default")) {
      return {};
    }
    value = expandHome(value);
    // A relative directory is relative to the config file's own directory.
    if (QDir::isRelativePath(value)) {
      value = QDir(QFileInfo(configPath).absolutePath()).absoluteFilePath(value);
    }
    const QString cleaned = QDir::cleanPath(value);
    // Kartend-b2hi9: keep the launch surface's "every path is validated"
    // invariant. These keys come from a config file that is normally the
    // user's own, but can be third-party (synced dotfiles, a shipped image,
    // a kart bundling a retroarch.cfg path override) — reject a value carrying
    // shell metachars / NUL before it drives core discovery + the -L argument,
    // or (for assets_directory) a tree of icon paths handed to QPixmap.
    // A bad value is treated as unset so discovery falls through to the probe.
    if (PathUtils::validatePathSecurity(cleaned).isError()) {
      return {};
    }
    return cleaned;
  }
  return {};
}

// True when `dir` looks like a RetroArch assets tree rather than some other
// directory the user happened to point at. The icon packs live under `xmb/`;
// `ozone/` and `glui/` are the other menu drivers' asset roots and are enough
// to identify the tree even on an install whose xmb assets were never
// downloaded.
bool looksLikeAssetsTree(const QString &dir) {
  if (dir.isEmpty()) {
    return false;
  }
  const QDir probe(dir);
  return probe.exists(QStringLiteral("xmb")) || probe.exists(QStringLiteral("ozone")) ||
         probe.exists(QStringLiteral("glui"));
}

} // namespace

QString coreDirectoryFromConfig(const QString &configPath) {
  return directoryFromConfig(configPath, QLatin1StringView("libretro_directory"));
}

QString assetsDirectoryFromConfig(const QString &configPath) {
  const QString fromKey = directoryFromConfig(configPath, QLatin1StringView("assets_directory"));
  if (!fromKey.isEmpty()) {
    return fromKey;
  }
  // No usable key. RetroArch only writes assets_directory once the path has
  // been resolved, and the assets tree lives beside the config either way, so
  // a sibling `assets/` is the honest fallback rather than giving up. Held to
  // the same shape check as an explicit override so this cannot silently
  // return an unrelated directory that happens to be named `assets`.
  if (configPath.isEmpty()) {
    return {};
  }
  const QString sibling = QDir::cleanPath(
      QDir(QFileInfo(configPath).absolutePath()).filePath(QStringLiteral("assets")));
  return looksLikeAssetsTree(sibling) ? sibling : QString();
}

QString resolveCoreDirectory(const QString &overridePath) {
  const QString trimmedOverride = expandHome(overridePath.trimmed());
  if (!trimmedOverride.isEmpty()) {
    const QFileInfo info(trimmedOverride);
    if (info.isDir()) {
      return QDir::cleanPath(info.absoluteFilePath());
    }
    if (info.isFile()) {
      return coreDirectoryFromConfig(trimmedOverride);
    }
    // A non-existent override is treated as "unset" — fall through to
    // the standard probe rather than returning a dead path.
  }

  for (const QString &candidate : defaultConfigPaths()) {
    if (!QFileInfo::exists(candidate)) {
      continue;
    }
    const QString coreDir = coreDirectoryFromConfig(candidate);
    if (!coreDir.isEmpty() && QFileInfo(coreDir).isDir()) {
      return coreDir;
    }
  }
  return {};
}

QString resolveAssetsDirectory(const QString &overridePath) {
  const QString trimmedOverride = expandHome(overridePath.trimmed());
  if (!trimmedOverride.isEmpty()) {
    const QFileInfo info(trimmedOverride);
    // A DIRECTORY override is ambiguous in a way the core-directory case is
    // not: the same setting is documented as "your RetroArch install", so it
    // can point at the assets tree itself OR at the install root that holds
    // both the config and the assets. Accept the tree when it looks like one,
    // otherwise look for a retroarch.cfg inside and read the key from there.
    if (info.isDir()) {
      const QString dir = QDir::cleanPath(info.absoluteFilePath());
      if (looksLikeAssetsTree(dir)) {
        return dir;
      }
      const QString nestedConfig = QDir(dir).filePath(QStringLiteral("retroarch.cfg"));
      if (QFileInfo::exists(nestedConfig)) {
        const QString fromNested = assetsDirectoryFromConfig(nestedConfig);
        if (!fromNested.isEmpty() && QFileInfo(fromNested).isDir()) {
          return fromNested;
        }
      }
    } else if (info.isFile()) {
      const QString fromConfig = assetsDirectoryFromConfig(trimmedOverride);
      if (!fromConfig.isEmpty() && QFileInfo(fromConfig).isDir()) {
        return fromConfig;
      }
    }
    // An override that resolves to nothing usable falls through to the
    // standard probe rather than returning a dead path — the same contract
    // resolveCoreDirectory gives a non-existent override.
  }

  for (const QString &candidate : defaultConfigPaths()) {
    if (!QFileInfo::exists(candidate)) {
      continue;
    }
    const QString assetsDir = assetsDirectoryFromConfig(candidate);
    if (!assetsDir.isEmpty() && QFileInfo(assetsDir).isDir()) {
      return assetsDir;
    }
  }
  return {};
}

QList<Core> discoverCores(const QString &coreDirectory) {
  QList<Core> cores;
  if (coreDirectory.isEmpty()) {
    return cores;
  }
  QDir dir(coreDirectory);
  if (!dir.exists()) {
    return cores;
  }
  QStringList nameFilters;
  nameFilters.reserve(coreExtensions().size());
  for (const QString &ext : coreExtensions()) {
    nameFilters << QStringLiteral("*.") + ext;
  }
  const QFileInfoList entries =
      dir.entryInfoList(nameFilters, QDir::Files | QDir::NoSymLinks, QDir::Name);
  // Kartend-m8jfc: cap the entries processed so a pathological core directory
  // (tens of thousands of .so/.dll/.dylib files) yields a bounded result set
  // rather than blocking the GUI thread for the full enumeration. Mirrors
  // UIConstants::Launch::MAX_EXTRACTION_FILES_INSPECTED. A normal install is far
  // under this ceiling, so no real core is dropped.
  int inspected = 0;
  for (const QFileInfo &entry : entries) {
    if (++inspected > kMaxCoresInspected) {
      break;
    }
    Core core;
    core.path = entry.absoluteFilePath();
    // Cores ship as `<name>_libretro.<ext>`; the short name is what
    // users recognise, so drop the tag and extension.
    QString name = entry.completeBaseName();
    if (name.endsWith(QStringLiteral("_libretro"), Qt::CaseInsensitive)) {
      name.chop(QStringLiteral("_libretro").size());
    }
    core.displayName = name.isEmpty() ? entry.fileName() : name;
    cores.append(core);
  }
  std::sort(cores.begin(), cores.end(), [](const Core &a, const Core &b) {
    return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
  });
  return cores;
}

} // namespace RetroArchUtils
