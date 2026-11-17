#include "esdelibrary.h"

#include "extensionutils.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace EsdeLibrary {

namespace {

/// Ceiling on es_settings.xml (Kartend-v3u04). A local file, so hygiene rather
/// than an attack path — but the read below is readAll() plus a prepend and an
/// append, which is three copies of the buffer live at once. A stock 3.4.1
/// settings file is ~171 short elements; 8 MiB is orders of magnitude clear of
/// that while keeping the transient bounded.
constexpr qint64 kMaxSettingsBytes = 8LL * 1024 * 1024;

/// Reads one `<string name="KEY" value="VALUE" />` out of es_settings.xml.
/// Returns an empty string both when the key is absent and when its value is
/// empty — the caller cannot distinguish them and does not need to, because
/// ES-DE writes an empty value to mean "use the default".
QString settingValue(const QString &settingsPath, const QString &key) {
  QFile file(settingsPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  if (file.size() > kMaxSettingsBytes) {
    return {}; // absent and empty are already indistinguishable to the caller
  }
  // es_settings.xml IS NOT WELL-FORMED XML: ES-DE writes a bare, alphabetically
  // sorted sequence of <bool>/<string>/<int> elements with NO root element (171
  // of them on a stock 3.4.1 install). A conforming parser reads the first
  // element and then stops with "extra content at end of document" — which
  // would find neither ROMDirectory nor MediaDirectory, since both sort well
  // past the top. Wrapping the body in a synthetic root makes it parseable
  // while keeping proper attribute unescaping.
  QByteArray body = file.readAll();
  const int declarationEnd = body.indexOf("?>");
  if (declarationEnd >= 0) {
    body = body.mid(declarationEnd + 2);
  }
  body.prepend("<esSettings>");
  body.append("</esSettings>");

  QXmlStreamReader xml(body);
  // Kartend-v3u04: pin the entity-expansion ceiling instead of inheriting it.
  // QXmlStreamReader resolves internal entities, so a recursive declaration is
  // the billion-laughs shape; Qt already defaults this to 4096 characters, and
  // 4096 is what we want — the point is that it is now a decision this project
  // made, not one a future Qt version could change under us.
  xml.setEntityExpansionLimit(4096);
  while (!xml.atEnd() && !xml.hasError()) {
    if (xml.readNext() != QXmlStreamReader::StartElement) {
      continue;
    }
    // ES-DE writes typed elements (<string>, <bool>, <int>); the name
    // attribute is what identifies the setting, whatever the type.
    const QXmlStreamAttributes attributes = xml.attributes();
    if (attributes.value(QStringLiteral("name")) == key) {
      return attributes.value(QStringLiteral("value")).toString().trimmed();
    }
  }
  return {};
}

QString expandTilde(const QString &path) {
  if (path == QLatin1String("~")) {
    return QDir::homePath();
  }
  if (path.startsWith(QLatin1String("~/"))) {
    return QDir::homePath() + path.mid(1);
  }
  return path;
}

/// gamelist <path> values are "./Game.nes" — relative to the SYSTEM's ROM
/// directory, not to the gamelist. Normalised to a bare file name so a scanned
/// file can be matched against it.
QString gamelistKey(const QString &rawPath) {
  QString key = rawPath.trimmed();
  while (key.startsWith(QLatin1String("./"))) {
    key.remove(0, 2);
  }
  // Subdirectory entries keep their relative form; the file name alone would
  // collide across folders.
  return key;
}

struct GamelistEntry {
  QString name;
  QString description;
  QString developer;
  QString publisher;
  QString genre;
  QString players;
  QString releaseDate;
  bool hidden = false;
  // Raw media tags (Kartend-1bwzs). Only the per-folder reader uses them; the
  // ES-DE source keeps finding media by naming convention.
  QString boxart;
  QString thumbnail;
  QString marquee;
  QString wheel;
  QString fanart;
  QString image;
};

struct GamelistRecord {
  QString path; ///< <path> verbatim (trimmed).
  GamelistEntry entry;
};

/// Parses one gamelist.xml into its <game> records, in document order. A
/// missing file yields nothing; a malformed one yields the records completed
/// before the error.
QList<GamelistRecord> parseGamelistFile(const QString &gamelistPath) {
  QList<GamelistRecord> records;
  QFile file(gamelistPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return records;
  }
  QXmlStreamReader xml(&file);
  // Kartend-v3u04: pin the entity-expansion ceiling instead of inheriting it.
  // QXmlStreamReader resolves internal entities, so a recursive declaration is
  // the billion-laughs shape; Qt already defaults this to 4096 characters, and
  // 4096 is what we want — the point is that it is now a decision this project
  // made, not one a future Qt version could change under us.
  xml.setEntityExpansionLimit(4096);
  while (!xml.atEnd() && !xml.hasError()) {
    if (xml.readNext() != QXmlStreamReader::StartElement || xml.name() != QLatin1String("game")) {
      continue;
    }
    QString path;
    GamelistEntry entry;
    while (
        !(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QLatin1String("game")) &&
        !xml.atEnd() && !xml.hasError()) {
      xml.readNext();
      if (xml.tokenType() != QXmlStreamReader::StartElement) {
        continue;
      }
      const QString tag = xml.name().toString();
      const QString text = xml.readElementText().trimmed();
      if (tag == QLatin1String("path")) {
        path = text;
      } else if (tag == QLatin1String("name")) {
        entry.name = text;
      } else if (tag == QLatin1String("desc")) {
        entry.description = text;
      } else if (tag == QLatin1String("developer")) {
        entry.developer = text;
      } else if (tag == QLatin1String("publisher")) {
        entry.publisher = text;
      } else if (tag == QLatin1String("genre")) {
        entry.genre = text;
      } else if (tag == QLatin1String("players")) {
        entry.players = text;
      } else if (tag == QLatin1String("releasedate")) {
        entry.releaseDate = text;
      } else if (tag == QLatin1String("hidden")) {
        entry.hidden = (text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
      } else if (tag == QLatin1String("boxart")) {
        entry.boxart = text;
      } else if (tag == QLatin1String("thumbnail")) {
        entry.thumbnail = text;
      } else if (tag == QLatin1String("marquee")) {
        entry.marquee = text;
      } else if (tag == QLatin1String("wheel")) {
        entry.wheel = text;
      } else if (tag == QLatin1String("fanart")) {
        entry.fanart = text;
      } else if (tag == QLatin1String("image")) {
        entry.image = text;
      }
    }
    if (!path.isEmpty()) {
      records.append({path, entry});
    }
  }
  return records;
}

/// Parses `<dataDir>/gamelists/<system>/gamelist.xml` into a by-relative-path
/// map. A missing or malformed file yields an empty map: the gamelist is an
/// OVERLAY, so its absence must not hide the games themselves.
QHash<QString, GamelistEntry> readGamelist(const QString &dataDir, const QString &systemName) {
  QHash<QString, GamelistEntry> entries;
  const QList<GamelistRecord> records = parseGamelistFile(
      dataDir + QStringLiteral("/gamelists/") + systemName + QStringLiteral("/gamelist.xml"));
  for (const GamelistRecord &record : records) {
    // Later duplicates replace earlier ones, as before.
    entries[gamelistKey(record.path)] = record.entry;
  }
  return entries;
}

/// Canonical form of an EXISTING path with every symlink, junction and other
/// filesystem link resolved, '/'-separated; empty when it does not resolve
/// (Kartend-1bwzs). std::filesystem rather than QFileInfo::canonicalFilePath:
/// on Windows the latter's handling of reparse points differs between Qt
/// versions (see test_kartreader.cpp), while std::filesystem::canonical asks
/// the OS for the final path of the opened file, junctions included, and
/// realpath()s on POSIX. The root and every candidate go through this one
/// function, so the prefix comparison below compares like with like.
QString resolvedPath(const QString &path) {
  std::error_code error;
  const std::filesystem::path resolved =
      std::filesystem::canonical(std::filesystem::path(path.toStdU16String()), error);
  if (error) {
    return {};
  }
  return QString::fromStdU16String(resolved.generic_u16string());
}

/// Resolves a gamelist-relative value against `resolvedRoot` (Kartend-1bwzs).
/// Empty unless the value is relative on EVERY platform, has no ".." segment,
/// names an existing entry, and that entry's resolved target is strictly
/// inside the root — the last check is what defeats a link pointing out of
/// the system folder.
QString confinedPath(const QString &resolvedRoot, const QString &rawValue) {
  const QString value = rawValue.trimmed();
  if (value.isEmpty() || QDir::isAbsolutePath(value) || value.startsWith(QLatin1Char('\\'))) {
    return {};
  }
  // Backslash counts as a separator here only: a list written on Windows must
  // not smuggle "..\" past the check, but the value is not otherwise rewritten.
  static const QRegularExpression kSeparator(QStringLiteral("[/\\\\]"));
  const QStringList segments = value.split(kSeparator);
  if (segments.contains(QStringLiteral(".."))) {
    return {};
  }
  // A drive designator ("C:/x", "C:x") is absolute or drive-relative on
  // Windows but an ordinary relative name elsewhere; no gamelist writer puts
  // one in a relative path, so it is refused everywhere and a list resolves
  // the same way on every host. Only the letter-colon shape: "Zelda: X.nes"
  // is a legal name on Linux and stays admitted.
  static const QRegularExpression kDriveDesignator(QStringLiteral("^[A-Za-z]:"));
  if (kDriveDesignator.match(value).hasMatch()) {
    return {};
  }
  const QString resolved = resolvedPath(resolvedRoot + QLatin1Char('/') + value);
  const QString prefix =
      resolvedRoot.endsWith(QLatin1Char('/')) ? resolvedRoot : resolvedRoot + QLatin1Char('/');
  if (resolved.isEmpty() || !resolved.startsWith(prefix, Qt::CaseSensitive)) {
    return {};
  }
  return resolved;
}

/// The first of `candidates` that confines to an existing FILE under the root.
QString firstMedia(const QString &resolvedRoot, std::initializer_list<QString> candidates) {
  for (const QString &candidate : candidates) {
    const QString resolved = confinedPath(resolvedRoot, candidate);
    if (!resolved.isEmpty() && QFileInfo(resolved).isFile()) {
      return resolved;
    }
  }
  return {};
}

/// downloaded_media/<system>/<type>/<rom base name>.<ext>. ES-DE writes jpg
/// and png; both are probed rather than assumed.
QString mediaFor(const QString &mediaDir, const QString &systemName, const QString &type,
                 const QString &baseName) {
  if (mediaDir.isEmpty()) {
    return {};
  }
  const QString stem = mediaDir + QLatin1Char('/') + systemName + QLatin1Char('/') + type +
                       QLatin1Char('/') + baseName;
  for (const QString &extension : {QStringLiteral(".png"), QStringLiteral(".jpg"),
                                   QStringLiteral(".jpeg"), QStringLiteral(".webp")}) {
    if (QFileInfo::exists(stem + extension)) {
      return stem + extension;
    }
  }
  return {};
}

/// downloaded_media/<system>/videos/<rom base name>.<ext> (Kartend-vtz2h).
/// Probed with the video extensions the rest of Kartend plays, in their order,
/// so the import admits exactly what the details pane would look for.
QString videoFor(const QString &mediaDir, const QString &systemName, const QString &baseName) {
  if (mediaDir.isEmpty()) {
    return {};
  }
  const QDir dir(mediaDir + QLatin1Char('/') + systemName + QStringLiteral("/videos"));
  for (const QString &extension : ExtensionUtils::videoBaseExtensions()) {
    const QString path = dir.path() + QLatin1Char('/') + baseName + QLatin1Char('.') + extension;
    if (QFileInfo(path).isFile()) {
      return path;
    }
  }
  return {};
}

} // namespace

auto romExtensions() -> const QStringList & {
  static const QStringList kExtensions = {
      // Cartridge/handheld images, the common ones across ES-DE's systems.
      QStringLiteral("nes"),
      QStringLiteral("fds"),
      QStringLiteral("unf"),
      QStringLiteral("sfc"),
      QStringLiteral("smc"),
      QStringLiteral("gb"),
      QStringLiteral("gbc"),
      QStringLiteral("gba"),
      QStringLiteral("nds"),
      QStringLiteral("n64"),
      QStringLiteral("z64"),
      QStringLiteral("v64"),
      QStringLiteral("gg"),
      QStringLiteral("sms"),
      QStringLiteral("md"),
      QStringLiteral("gen"),
      QStringLiteral("smd"),
      QStringLiteral("32x"),
      QStringLiteral("pce"),
      QStringLiteral("sgx"),
      QStringLiteral("ws"),
      QStringLiteral("wsc"),
      QStringLiteral("ngp"),
      QStringLiteral("ngc"),
      QStringLiteral("a26"),
      QStringLiteral("a78"),
      QStringLiteral("lnx"),
      QStringLiteral("vb"),
      QStringLiteral("vec"),
      QStringLiteral("col"),
      QStringLiteral("int"),
      QStringLiteral("d64"),
      QStringLiteral("tap"),
      QStringLiteral("t64"),
      QStringLiteral("crt"),
      QStringLiteral("adf"),
      QStringLiteral("dsk"),
      QStringLiteral("st"),
      QStringLiteral("z80"),
      // Disc images and containers.
      QStringLiteral("iso"),
      QStringLiteral("cue"),
      QStringLiteral("bin"),
      QStringLiteral("chd"),
      QStringLiteral("cso"),
      QStringLiteral("rvz"),
      QStringLiteral("wbfs"),
      QStringLiteral("gcm"),
      QStringLiteral("gcz"),
      QStringLiteral("pbp"),
      QStringLiteral("m3u"),
      QStringLiteral("ciso"),
      // Archives ES-DE hands straight to the emulator.
      QStringLiteral("zip"),
      QStringLiteral("7z"),
      QStringLiteral("rar"),
      // Ports and script-launched systems.
      QStringLiteral("sh"),
      QStringLiteral("desktop"),
  };
  return kExtensions;
}

auto defaultDataDir() -> QString {
  const QStringList candidates = {
      QDir::homePath() + QStringLiteral("/ES-DE"),             // 3.0 and later
      QDir::homePath() + QStringLiteral("/.emulationstation"), // legacy layout
  };
  for (const QString &dir : candidates) {
    // settings/ is created on first run, so it marks a real install rather
    // than a directory a user happened to make.
    if (QFileInfo::exists(dir + QStringLiteral("/settings/es_settings.xml"))) {
      return dir;
    }
  }
  return {};
}

auto romDirectory(const QString &dataDir) -> QString {
  const QString configured = settingValue(dataDir + QStringLiteral("/settings/es_settings.xml"),
                                          QStringLiteral("ROMDirectory"));
  if (!configured.isEmpty()) {
    return QDir::cleanPath(expandTilde(configured));
  }
  // Empty is how a stock install stores "use the default" — verified on a real
  // ES-DE 3.4.1 install, where the element is present with value="".
  return QDir::homePath() + QStringLiteral("/ROMs");
}

auto mediaDirectory(const QString &dataDir) -> QString {
  const QString configured = settingValue(dataDir + QStringLiteral("/settings/es_settings.xml"),
                                          QStringLiteral("MediaDirectory"));
  if (!configured.isEmpty()) {
    return QDir::cleanPath(expandTilde(configured));
  }
  return dataDir + QStringLiteral("/downloaded_media");
}

auto systems(const QString &romDir) -> QList<System> {
  QList<System> found;
  const QDir root(romDir);
  if (!root.exists()) {
    return found;
  }
  // ES-DE drops a per-system README into an empty ROM tree on first run; a
  // directory holding nothing but those is not a system worth importing.
  const QStringList names = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString &name : names) {
    System system;
    system.name = name;
    system.romDir = root.filePath(name);
    const QDir systemDir(system.romDir);
    for (const QFileInfo &info :
         systemDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
      if (romExtensions().contains(info.suffix().toLower())) {
        ++system.gameCount;
      }
    }
    if (system.gameCount > 0) {
      found.append(system);
    }
  }
  return found;
}

auto games(const System &system, const QString &dataDir, const QString &mediaDir) -> QList<Game> {
  QList<Game> result;
  const QHash<QString, GamelistEntry> overlay = readGamelist(dataDir, system.name);
  const QDir systemDir(system.romDir);
  for (const QFileInfo &info :
       systemDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
    if (!romExtensions().contains(info.suffix().toLower())) {
      continue;
    }
    const GamelistEntry entry = overlay.value(info.fileName());
    if (entry.hidden) {
      continue; // ES-DE hides it too
    }
    Game game;
    game.romPath = info.absoluteFilePath();
    game.title = entry.name.isEmpty() ? info.completeBaseName() : entry.name;
    game.description = entry.description;
    game.developer = entry.developer;
    game.publisher = entry.publisher;
    game.genre = entry.genre;
    game.players = entry.players;
    game.releaseDate = entry.releaseDate;
    // Media is keyed on the ROM's base name, not the display title.
    const QString base = info.completeBaseName();
    game.coverPath = mediaFor(mediaDir, system.name, QStringLiteral("covers"), base);
    game.logoPath = mediaFor(mediaDir, system.name, QStringLiteral("marquees"), base);
    game.screenshotPath = mediaFor(mediaDir, system.name, QStringLiteral("screenshots"), base);
    game.fanartPath = mediaFor(mediaDir, system.name, QStringLiteral("fanart"), base);
    game.videoPath = videoFor(mediaDir, system.name, base);
    result.append(game);
  }
  std::sort(result.begin(), result.end(),
            [](const Game &a, const Game &b) { return a.title.localeAwareCompare(b.title) < 0; });
  return result;
}

auto readFolderGamelist(const QString &gamelistPath, const QString &systemRoot)
    -> QList<FolderGamelistGame> {
  QList<FolderGamelistGame> result;
  const QString resolvedRoot = resolvedPath(systemRoot);
  if (resolvedRoot.isEmpty() || !QFileInfo(resolvedRoot).isDir()) {
    return result;
  }
  for (const GamelistRecord &record : parseGamelistFile(gamelistPath)) {
    const QString romPath = confinedPath(resolvedRoot, record.path);
    if (romPath.isEmpty()) {
      continue; // refused or missing: nothing to launch
    }
    const GamelistEntry &entry = record.entry;
    FolderGamelistGame game;
    game.romPath = romPath;
    game.name = entry.name;
    game.description = entry.description;
    game.developer = entry.developer;
    game.publisher = entry.publisher;
    game.genre = entry.genre;
    game.players = entry.players;
    game.releaseDate = entry.releaseDate;
    game.hidden = entry.hidden;
    // An unusable preferred tag falls through to its fallback rather than
    // emptying the slot outright.
    game.coverPath = firstMedia(resolvedRoot, {entry.boxart, entry.thumbnail});
    game.logoPath = firstMedia(resolvedRoot, {entry.marquee, entry.wheel});
    game.heroPath = firstMedia(resolvedRoot, {entry.fanart, entry.image});
    result.append(game);
  }
  return result;
}

} // namespace EsdeLibrary
