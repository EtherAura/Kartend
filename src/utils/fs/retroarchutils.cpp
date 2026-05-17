// Discovery of a RetroArch installation's libretro cores. See the
// header for the resolution order; the cfg parse is a deliberately
// small line scanner — retroarch.cfg is a flat `key = "value"` file.
#include "retroarchutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

#include <algorithm>

namespace RetroArchUtils {

namespace {

// Libretro core file extensions across the three desktop platforms.
const QStringList &coreExtensions() {
  static const QStringList kExts = {QStringLiteral("so"), QStringLiteral("dll"),
                                    QStringLiteral("dylib")};
  return kExts;
}

// retroarch.cfg routinely stores paths with a leading `~` for the home
// directory; QDir / QFileInfo do NOT expand it, so a `~`-rooted core
// directory would look non-existent. Expand it here.
QString expandHome(const QString &path) {
  if (path == QStringLiteral("~")) {
    return QDir::homePath();
  }
  if (path.startsWith(QStringLiteral("~/"))) {
    return QDir::homePath() + path.mid(1);
  }
  return path;
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

QString coreDirectoryFromConfig(const QString &configPath) {
  QFile file(configPath);
  if (configPath.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  QTextStream stream(&file);
  while (!stream.atEnd()) {
    const QString line = stream.readLine().trimmed();
    if (!line.startsWith(QStringLiteral("libretro_directory"))) {
      continue;
    }
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq < 0) {
      continue;
    }
    QString value = line.mid(eq + 1).trimmed();
    // retroarch.cfg quotes string values; strip a matched pair.
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) &&
        value.endsWith(QLatin1Char('"'))) {
      value = value.mid(1, value.size() - 2);
    }
    // RetroArch writes "default" (or leaves it blank) when the core
    // dir hasn't been customised — not a real path.
    if (value.isEmpty() || value == QStringLiteral("default")) {
      return {};
    }
    value = expandHome(value);
    // A relative core dir is relative to the config file's directory.
    if (QDir::isRelativePath(value)) {
      value = QDir(QFileInfo(configPath).absolutePath()).absoluteFilePath(value);
    }
    return QDir::cleanPath(value);
  }
  return {};
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
  for (const QFileInfo &entry : entries) {
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
