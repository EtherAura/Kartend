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

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace KdeColorScheme {

namespace {
/// Shared "r,g,b" reader for kdeglobals keys, e.g. "WM/activeBackground".
///
/// Parsed by hand rather than through QSettings: QSettings keeps a cached
/// QConfFile per path and will happily serve a stale value for a file
/// rewritten moments earlier, which is precisely the case here — Plasma
/// rewrites kdeglobals whenever the desktop colours change, and the app has
/// to see the NEW colour immediately (user request 2026-08-19). Reading the
/// file directly also matches how this module already handles .colors.
///
/// Deliberately NOT cached in a static: the whole point is that the desktop
/// colour changes under a running app (a Plasma activity switch with a
/// per-activity wallpaper). This is a handful of lines of INI parsed on a
/// re-theme, not on a paint.
QColor readKdeGlobalsColor(const QString &key) {
  const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                       QStringLiteral("/kdeglobals");
  const int slash = key.indexOf(QLatin1Char('/'));
  if (slash <= 0) {
    return {};
  }
  const QString wantSection = key.left(slash);
  const QString wantKey = key.mid(slash + 1);

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  QTextStream in(&file);
  QString section;
  QString value;
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
      continue;
    }
    if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
      section = line.mid(1, line.size() - 2);
      continue;
    }
    if (section != wantSection) {
      continue;
    }
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq <= 0) {
      continue;
    }
    if (line.left(eq).trimmed() == wantKey) {
      value = line.mid(eq + 1).trimmed();
      break;
    }
  }
  if (value.isEmpty()) {
    return {};
  }

  const QStringList parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
  if (parts.size() < 3) {
    return {};
  }
  bool okR = false;
  bool okG = false;
  bool okB = false;
  const int r = parts.at(0).trimmed().toInt(&okR);
  const int g = parts.at(1).trimmed().toInt(&okG);
  const int b = parts.at(2).trimmed().toInt(&okB);
  if (!okR || !okG || !okB) {
    return {};
  }
  return QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}
} // namespace

QColor desktopAccentColor() {
  return readKdeGlobalsColor(QStringLiteral("General/AccentColor"));
}

// Kartend-5w1zb: HEADER FIRST, [WM] ONLY AS A FALLBACK.
//
// Breeze has drawn the window decoration from the Colors:Header group since
// Plasma 5.23; [WM] is the pre-5.23 key and is no longer what the titlebar
// you can see is painted with. Reading [WM] therefore produced chrome that was
// CLOSE to the decoration but not equal to it, and the seam was visible where
// the toolbar meets the titlebar. Measured under Breeze Dark:
//
//   [Colors:Header] BackgroundNormal = 41,44,48   <- what KWin actually renders
//   [WM]            activeBackground = 39,44,49   <- what we used to read
//
// Sampling the framebuffer confirmed the decoration at (41,44,48) against our
// menu bar, toolbar and sidebar all at (39,44,49) — a 2/255 red, 1/255 blue
// mismatch, small but exactly the "titlebar does not match the toolbar"
// report. This is a KEY CHOICE, not a fudge: no offset is applied anywhere, so
// a scheme whose Header and WM groups agree is unaffected, and a scheme that
// omits Header (anything pre-5.23, or a hand-written .colors) still resolves
// through the old key.
//
// Neither read is cached — see readKdeGlobalsColor. That is what makes a
// Plasma activity switch, or any scheme change, repaint in the new colours
// without a restart.
QColor activeTitlebarTextColor() {
  if (const QColor header = readKdeGlobalsColor(QStringLiteral("Colors:Header/ForegroundNormal"));
      header.isValid()) {
    return header;
  }
  return readKdeGlobalsColor(QStringLiteral("WM/activeForeground"));
}

QColor activeTitlebarColor() {
  if (const QColor header = readKdeGlobalsColor(QStringLiteral("Colors:Header/BackgroundNormal"));
      header.isValid()) {
    return header;
  }
  return readKdeGlobalsColor(QStringLiteral("WM/activeBackground"));
}

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
  assignIfPresent(config.background.backgroundColor, scheme, "View", "BackgroundNormal");
  assignIfPresent(config.background.primaryColor, scheme, "Window", "BackgroundNormal");
  assignIfPresent(config.background.tileColor, scheme, "View", "BackgroundAlternate");
  assignIfPresent(config.background.selectionColor, scheme, "Selection", "BackgroundNormal");
  assignIfPresent(config.listView.listRowColor, scheme, "View", "BackgroundNormal");
  assignIfPresent(config.listView.listAltRowColor, scheme, "View", "BackgroundAlternate");
  assignIfPresent(config.sidebar.sidebarBackgroundColor, scheme, "Window", "BackgroundAlternate");
  assignIfPresent(config.sidebar.sidebarTextColor, scheme, "Window", "ForegroundNormal");
  assignIfPresent(config.sidebar.sidebarAccentColor, scheme, "Selection", "BackgroundNormal");
  assignIfPresent(config.sidebar.sidebarPatternColor, scheme, "Window", "BackgroundNormal");
  assignIfPresent(config.sidebar.sidebarHeaderBgColor, scheme, "Window", "BackgroundAlternate");
  assignIfPresent(config.sidebar.sidebarSectionBgColor, scheme, "View", "BackgroundAlternate");
}

void applyToGeneralSettings(const Scheme &scheme, GeneralSettings &settings) {
  assignIfPresent(settings.appearance.titleBaseColor, scheme, "Selection", "BackgroundNormal");
}

} // namespace KdeColorScheme
