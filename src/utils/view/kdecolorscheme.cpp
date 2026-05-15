// KDE color-scheme loader. Parses .colors files (the INI dialect Plasma
// uses for its scheme presets) into a typed Scheme + maps the well-known
// section/key combos onto Kartend's per-collection appearance fields.
//
// Why we don't use QSettings directly: .colors files live both on disk
// (under $XDG_DATA_DIRS/color-schemes) and in our Qt resource system
// (`:/themes/...` for the bundled fallbacks). QSettings handles the
// filesystem case but not QRC paths, so we route everything through a
// QFile + manual section/key parser. The format is simple enough that
// rolling our own avoids a tempfile-shuffle dance and lets us treat
// resource and filesystem schemes uniformly.
#include "kdecolorscheme.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

#include "collectionutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace KdeColorScheme {

namespace {

/// Absolute paths of the bundled .colors files inside the Qt resource
/// system. Adding a new bundled scheme means adding it both here and in
/// src/assets/resources.qrc; the picker dedupe + sort logic handles the
/// rest.
QStringList bundledResourcePaths() {
  return {
      QStringLiteral(":/themes/KartendDark.colors"),
      QStringLiteral(":/themes/KartendLight.colors"),
      QStringLiteral(":/themes/KartendNeon.colors"),
  };
}

/// Splits an "R,G,B" or "R,G,B,A" string into a QColor. Returns an
/// invalid color when the input doesn't match the expected shape; the
/// loader skips invalid entries silently rather than rejecting the
/// whole file (a malformed key shouldn't kill an otherwise-usable
/// scheme).
QColor parseRgbTriple(const QString &raw) {
  const QStringList parts = raw.split(',', Qt::SkipEmptyParts);
  if (parts.size() < 3 || parts.size() > 4) {
    return {};
  }
  bool ok = true;
  const int r = parts[0].trimmed().toInt(&ok);
  if (!ok || r < 0 || r > 255) return {};
  const int g = parts[1].trimmed().toInt(&ok);
  if (!ok || g < 0 || g > 255) return {};
  const int b = parts[2].trimmed().toInt(&ok);
  if (!ok || b < 0 || b > 255) return {};
  int a = 255;
  if (parts.size() == 4) {
    a = parts[3].trimmed().toInt(&ok);
    if (!ok || a < 0 || a > 255) return {};
  }
  return QColor(r, g, b, a);
}

/// Strips the "Colors:" / "Color:" prefix so callers index with just
/// "View" / "Window" / "Selection" instead of the full section header.
QString normaliseSection(const QString &raw) {
  if (raw.startsWith(QLatin1String("Colors:"))) {
    return raw.mid(7);
  }
  return raw;
}

/// Read-and-parse the body of a .colors file out of any QIODevice. Tiny
/// INI parser — one section header at a time, key=value lines, comments
/// (`#` / `;`) skipped. Sub-section headers like `[Colors:Header][Inactive]`
/// (which Plasma uses for inactive variants) are intentionally NOT
/// recognised — we only want the active variants.
Scheme parseFromDevice(QIODevice &device) {
  Scheme scheme;
  QTextStream stream(&device);
  QString currentSection;
  while (!stream.atEnd()) {
    QString line = stream.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) {
      continue;
    }
    if (line.startsWith('[') && line.endsWith(']') && line.count('[') == 1) {
      currentSection = normaliseSection(line.mid(1, line.size() - 2));
      continue;
    }
    if (currentSection.isEmpty()) {
      continue;
    }
    const int eq = line.indexOf('=');
    if (eq <= 0) {
      continue;
    }
    const QString key = line.left(eq).trimmed();
    const QString value = line.mid(eq + 1).trimmed();
    if (currentSection == QLatin1String("General") && key == QLatin1String("Name")) {
      scheme.displayName = value;
      continue;
    }
    const QColor color = parseRgbTriple(value);
    if (color.isValid()) {
      scheme.colors.insert(currentSection + QLatin1Char('/') + key, color);
    }
  }
  return scheme;
}

/// Convert a raw .colors path into a SchemeInfo. Reads only the
/// [General]/Name line so discovery doesn't pay the cost of a full
/// color-table parse for every scheme on disk.
SchemeInfo discoverInfo(const QString &filePath, bool bundled) {
  SchemeInfo info;
  info.filePath = filePath;
  info.isBundled = bundled;

  QFile f(filePath);
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream stream(&f);
    QString currentSection;
    while (!stream.atEnd()) {
      const QString line = stream.readLine().trimmed();
      if (line.startsWith('[') && line.endsWith(']')) {
        currentSection = line.mid(1, line.size() - 2);
        continue;
      }
      if (currentSection == QLatin1String("General") && line.startsWith(QLatin1String("Name="))) {
        info.displayName = line.mid(5).trimmed();
        break;
      }
    }
    f.close();
  }
  if (info.displayName.isEmpty()) {
    info.displayName = QFileInfo(filePath).completeBaseName();
  }
  return info;
}

} // namespace

bool Scheme::has(const QString &section, const QString &key) const {
  return colors.contains(section + QLatin1Char('/') + key);
}

QColor Scheme::color(const QString &section, const QString &key, const QColor &fallback) const {
  return colors.value(section + QLatin1Char('/') + key, fallback);
}

QList<SchemeInfo> discover() {
  QList<SchemeInfo> out;
  QSet<QString> seenNames;

  // Bundled first — they go to the top of the picker so a non-KDE
  // install always has visible options. Their displayName beats any
  // identical name from a system source (we only consider the first
  // hit for any given name).
  for (const QString &path : bundledResourcePaths()) {
    if (!QFile::exists(path)) {
      continue; // QRC entry missing at build time — skip rather than crash.
    }
    SchemeInfo info = discoverInfo(path, /*bundled=*/true);
    if (seenNames.contains(info.displayName)) {
      continue;
    }
    seenNames.insert(info.displayName);
    out.append(info);
  }

  // System-installed schemes via $XDG_DATA_DIRS — typically
  // /usr/share/color-schemes plus ~/.local/share/color-schemes when the
  // user has installed any. locateAll returns each matching directory
  // path; we walk for *.colors inside each.
  const QStringList dirs =
      QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                QStringLiteral("color-schemes"), QStandardPaths::LocateDirectory);
  for (const QString &dir : dirs) {
    QDir d(dir);
    const QStringList entries = d.entryList({QStringLiteral("*.colors")}, QDir::Files);
    for (const QString &entry : entries) {
      const QString absolute = d.absoluteFilePath(entry);
      SchemeInfo info = discoverInfo(absolute, /*bundled=*/false);
      if (seenNames.contains(info.displayName)) {
        continue;
      }
      seenNames.insert(info.displayName);
      out.append(info);
    }
  }

  // Sort: bundled block first (preserves their insertion order so the
  // dark/light/neon trio shows up in a deliberate sequence), then
  // system-installed alphabetically by displayName.
  std::stable_sort(out.begin(), out.end(), [](const SchemeInfo &a, const SchemeInfo &b) {
    if (a.isBundled != b.isBundled) {
      return a.isBundled; // bundled block first
    }
    if (a.isBundled) {
      return false; // preserve insertion order within bundled
    }
    return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
  });
  return out;
}

ErrorUtils::Result<Scheme> load(const QString &filePath) {
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Failed to open color scheme file",
                               "KdeColorScheme::load")
        .withDetails(QStringLiteral("Path: %1, Error: %2").arg(filePath, f.errorString()));
  }
  Scheme scheme = parseFromDevice(f);
  f.close();
  if (scheme.displayName.isEmpty()) {
    scheme.displayName = QFileInfo(filePath).completeBaseName();
  }
  if (scheme.colors.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Color scheme contained no parseable colors", "KdeColorScheme::load")
        .withDetails(QStringLiteral("Path: %1").arg(filePath));
  }
  return scheme;
}

namespace {

/// Helper: assign `target` from `scheme[section/key]` only when the
/// scheme actually has the entry. Leaves the field untouched on a
/// missing entry so applying a partial scheme is an additive merge
/// rather than a destructive overwrite.
void assignIfPresent(QString &target, const Scheme &scheme, const QString &section,
                     const QString &key) {
  if (scheme.has(section, key)) {
    target = scheme.color(section, key).name();
  }
}

} // namespace

void applyToCollection(const Scheme &scheme, CollectionConfig &config) {
  // The mapping below is the heuristic that gives the best visual fit
  // between Plasma's color slots and Kartend's per-collection fields.
  // It's imperfect by design — KDE schemes don't carry "tile" or
  // "sidebar header" colors, so we reuse the closest semantic match.
  // Users who don't like the auto-mapping can still tweak any single
  // field via the regular color picker after applying.
  assignIfPresent(config.backgroundColor, scheme, "View", "BackgroundNormal");
  assignIfPresent(config.primaryColor, scheme, "Window", "BackgroundNormal");
  assignIfPresent(config.tileColor, scheme, "View", "BackgroundAlternate");
  assignIfPresent(config.selectionColor, scheme, "Selection", "BackgroundNormal");
  assignIfPresent(config.listRowColor, scheme, "View", "BackgroundNormal");
  assignIfPresent(config.listAltRowColor, scheme, "View", "BackgroundAlternate");
  assignIfPresent(config.sidebarBackgroundColor, scheme, "Window", "BackgroundAlternate");
  assignIfPresent(config.sidebarTextColor, scheme, "Window", "ForegroundNormal");
  assignIfPresent(config.sidebarAccentColor, scheme, "Selection", "BackgroundNormal");
  assignIfPresent(config.sidebarPatternColor, scheme, "Window", "BackgroundNormal");
  assignIfPresent(config.sidebarHeaderBgColor, scheme, "Window", "BackgroundAlternate");
  assignIfPresent(config.sidebarSectionBgColor, scheme, "View", "BackgroundAlternate");
}

void applyToGeneralSettings(const Scheme &scheme, GeneralSettings &settings) {
  assignIfPresent(settings.titleBaseColor, scheme, "Selection", "BackgroundNormal");
}

} // namespace KdeColorScheme
