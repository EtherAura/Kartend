// Resolves settings file paths and provides INI file handling utilities.
#include "settingsutils.h"
#include "errorutils.h"
#include "pathutils.h"
#include "settingskeys.h"
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace keys = kartend::settings::keys;

namespace {

// Atomically replace destPath with the contents of srcPath: read the source
// into memory, write it through QSaveFile (which writes a temp file then does an
// atomic rename-replace on commit()), then fsync the parent directory so the
// rename survives a crash / power loss. Replaces the old
// copy-to-.tmp / remove-dest / rename dance, which left no destination at all if
// interrupted between the remove and the rename, and never fsync'd (Kartend-g2ox).
// `context` is the caller name woven into error messages.
ErrorUtils::Result<void> atomicReplaceFile(const QString &srcPath, const QString &destPath,
                                           const char *context) {
  QFile source(srcPath);
  if (!source.open(QIODevice::ReadOnly)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                           "Failed to open source file", context)
        .withDetails(QString("Source: %1, Error: %2").arg(srcPath, source.errorString()));
  }
  const QByteArray payload = source.readAll();
  source.close();

  QSaveFile out(destPath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to open destination file for writing", context)
        .withDetails(QString("Destination: %1, Error: %2").arg(destPath, out.errorString()));
  }
  if (out.write(payload) != payload.size()) {
    out.cancelWriting();
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to write complete destination file", context)
        .withDetails(QString("Destination: %1, Error: %2").arg(destPath, out.errorString()));
  }
  if (!out.commit()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to finalize destination file", context)
        .withDetails(QString("Destination: %1, Error: %2").arg(destPath, out.errorString()));
  }
  PathUtils::syncDirectory(QFileInfo(destPath).absolutePath());
  return ErrorUtils::Result<void>::success();
}

// The hand-rolled "conf" format trims values and treats '['/';'/'#'-prefixed
// and newline-bearing lines specially, so raw user strings (launch params,
// regexes, paths, font families) could not survive a write/read round-trip
// (Kartend-n777n). encodeIniValue / decodeIniValue add QSettings-equivalent
// percent-escaping that is a strict NO-OP for "plain" values, so existing
// on-disk config files (written without escaping) keep reading byte-identical.
//
// A value is written verbatim when it has no leading/trailing whitespace and
// contains none of the characters that the reader would otherwise mangle:
// '%' (the escape introducer itself), '=', CR, LF, and a leading '[', ';' or
// '#'. Anything else is percent-encoded. Because '%' itself forces encoding, a
// value written verbatim can never contain '%' — but that invariant only holds
// for files this encoder wrote. Pre-v0.0.15 builds wrote raw values unescaped,
// so a '%' on disk may be legacy user data; decodeIniValue disambiguates with
// a round-trip check (Kartend-309nh.6).
QString encodeIniValue(const QString &value) {
  bool needsEncoding = value != value.trimmed();
  if (!needsEncoding) {
    if (value.startsWith('[') || value.startsWith(';') || value.startsWith('#')) {
      needsEncoding = true;
    } else {
      for (const QChar ch : value) {
        if (ch == '%' || ch == '=' || ch == '\n' || ch == '\r') {
          needsEncoding = true;
          break;
        }
      }
    }
  }
  if (!needsEncoding) {
    return value; // Plain value: written exactly as before — back-compat.
  }

  QString out;
  out.reserve(value.size());
  for (int i = 0; i < value.size(); ++i) {
    const QChar ch = value.at(i);
    const bool atEdge = (i == 0 || i == value.size() - 1);
    const bool encodeChar = ch == '%' || ch == '=' || ch == '\n' || ch == '\r' ||
                            (atEdge && (ch == ' ' || ch == '\t')) ||
                            (i == 0 && (ch == '[' || ch == ';' || ch == '#'));
    if (encodeChar) {
      out += '%';
      out += QString::number(ch.unicode(), 16).rightJustified(2, '0').toUpper();
    } else {
      out += ch;
    }
  }
  return out;
}

// Reverse encodeIniValue. A raw value containing no '%' is a legacy/plain value
// (or a freshly-written plain value); both are .trimmed() to reproduce the
// historical reader behavior exactly. A '%'-bearing value is percent-decoded
// only when it could have been produced by encodeIniValue — verified by
// re-encoding the decoded result and requiring the original raw bytes back
// (the encoder is deterministic, so everything it wrote passes this check).
// A mismatch means the '%' is legacy user data from a pre-v0.0.15 build that
// wrote values unescaped (e.g. a literal "file%20name" launch parameter);
// decoding it would silently corrupt the value, so it is kept verbatim
// (trimmed, as the legacy reader did) and flagged via @p legacySuspect so the
// caller can snapshot the file before its first re-encoded rewrite. Accepted
// residue (Kartend-309nh.6): a legacy literal that happens to round-trip,
// like "100%25", still decodes.
QString decodeIniValue(const QString &raw, bool *legacySuspect) {
  if (!raw.contains('%')) {
    return raw.trimmed();
  }
  QString out;
  out.reserve(raw.size());
  for (int i = 0; i < raw.size(); ++i) {
    const QChar ch = raw.at(i);
    if (ch == '%' && i + 2 < raw.size()) {
      bool ok = false;
      const ushort code = static_cast<ushort>(raw.mid(i + 1, 2).toUInt(&ok, 16));
      if (ok) {
        out += QChar(code);
        i += 2;
        continue;
      }
    }
    out += ch;
  }
  if (encodeIniValue(out) != raw) {
    if (legacySuspect) *legacySuspect = true;
    return raw.trimmed();
  }
  return out;
}

// Config files whose last parse surfaced at least one legacy-suspect value
// (see decodeIniValue). writeIniFile consults this to snapshot such a file
// once before its first rewrite. Mutex-guarded: QSettings on our registered
// format may be driven from worker threads.
Q_GLOBAL_STATIC(QSet<QString>, g_legacySuspectFiles)
Q_GLOBAL_STATIC(QMutex, g_legacySuspectFilesMutex)

// One-time safety net for pre-v0.0.15 files: before the first rewrite of a
// file that loaded with legacy-suspect '%' values, copy the untouched on-disk
// bytes to "<path>.legacy.bak" (same shape as SettingsManager's corrupt-file
// snapshot and importConfig's .bak). writeIniFile rewrites the whole map
// re-encoded, so this is the last chance to preserve a value the round-trip
// heuristic could have misjudged. QSettings hands writeIniFile a QSaveFile,
// so the original file is still intact on disk at this point. An existing
// .legacy.bak is never overwritten — the earliest snapshot is the valuable
// one. Best-effort: a failed copy is logged, not retried.
void snapshotLegacySuspectFile(QIODevice &device) {
  const auto *fileDevice = qobject_cast<QFileDevice *>(&device);
  if (!fileDevice || fileDevice->fileName().isEmpty()) return;
  const QString path = fileDevice->fileName();
  {
    QMutexLocker lock(g_legacySuspectFilesMutex());
    if (!g_legacySuspectFiles()->remove(path)) return;
  }
  const QString backupPath = path + QStringLiteral(".legacy.bak");
  if (QFile::exists(backupPath) || !QFile::exists(path)) return;
  if (!QFile::copy(path, backupPath)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to snapshot legacy config before rewrite",
                                          "SettingsUtils::writeIniFile")
            .withDetails(QString("From: %1, To: %2").arg(path, backupPath)));
  }
}

bool readIniFile(QIODevice &device, QSettings::SettingsMap &map) {
  QTextStream in(&device);
  QString currentSection;
  bool legacySuspect = false;
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line.isEmpty() || line.startsWith(';') || line.startsWith('#')) continue;
    if (line.startsWith('[') && line.endsWith(']')) {
      currentSection = line.mid(1, line.length() - 2);
    } else {
      int eqPos = line.indexOf('=');
      if (eqPos != -1) {
        QString key = line.left(eqPos).trimmed();
        QString value = decodeIniValue(line.mid(eqPos + 1), &legacySuspect);
        if (!currentSection.isEmpty()) {
          map[currentSection + "/" + key] = value;
        } else {
          map[key] = value;
        }
      }
    }
  }
  if (legacySuspect) {
    // Remember this file so snapshotLegacySuspectFile can back it up before
    // the first rewrite re-encodes every value.
    if (const auto *fileDevice = qobject_cast<QFileDevice *>(&device);
        fileDevice && !fileDevice->fileName().isEmpty()) {
      QMutexLocker lock(g_legacySuspectFilesMutex());
      g_legacySuspectFiles()->insert(fileDevice->fileName());
    }
  }
  return true;
}

bool writeIniFile(QIODevice &device, const QSettings::SettingsMap &map) {
  snapshotLegacySuspectFile(device);
  QTextStream out(&device);
  QMap<QString, QMap<QString, QVariant>> sections;
  QMap<QString, QVariant> rootKeys;

  for (auto it = map.begin(); it != map.end(); ++it) {
    const QString &fullKey = it.key();
    int lastSlash = fullKey.lastIndexOf('/');
    if (lastSlash != -1) {
      QString section = fullKey.left(lastSlash);
      QString key = fullKey.mid(lastSlash + 1);
      sections[section][key] = it.value();
    } else {
      rootKeys[fullKey] = it.value();
    }
  }

  for (auto it = rootKeys.begin(); it != rootKeys.end(); ++it) {
    out << it.key() << "=" << encodeIniValue(it.value().toString()) << "\n";
  }
  if (!rootKeys.isEmpty() && !sections.isEmpty()) out << "\n";

  QStringList sectionNames = sections.keys();
  // QMap keys are already sorted, but we can ensure specific order if needed.
  // For now, alphabetical is fine and matches previous behavior.

  for (const QString &sectionName : sectionNames) {
    out << "[" << sectionName << "]\n";
    const auto &group = sections[sectionName];
    for (auto it = group.begin(); it != group.end(); ++it) {
      out << it.key() << "=" << encodeIniValue(it.value().toString()) << "\n";
    }
    out << "\n";
  }
  out.flush();
  return out.status() == QTextStream::Ok;
}
} // namespace

auto SettingsUtils::getFormat() -> QSettings::Format {
  static QSettings::Format format = QSettings::InvalidFormat;
  if (format == QSettings::InvalidFormat) {
    format = QSettings::registerFormat("conf", readIniFile, writeIniFile);
  }
  return format;
}

auto SettingsUtils::getConfigPath() -> QString {
  QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  QDir configDir(configRoot);
  QString appConfigPath = configDir.filePath("kartend");

  QDir appConfigDir(appConfigPath);
  if (!appConfigDir.exists() && !appConfigDir.mkpath(".")) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::ConfigSaveFailed,
                                          "Failed to create application config directory",
                                          "SettingsUtils::getConfigPath")
            .withDetails(QString("Path: %1").arg(appConfigPath)));
  }
  return appConfigDir.absoluteFilePath("kartend.cfg");
}

auto SettingsUtils::getLayoutProfilesPath() -> QString {
  // Sibling to kartend.cfg under the same per-user config dir. Re-using
  // getConfigPath()'s mkpath ensures the directory exists before the
  // caller's QSaveFile gets a chance to fail.
  const QFileInfo info(getConfigPath());
  return info.absoluteDir().absoluteFilePath("layout_profiles.json");
}

auto SettingsUtils::getPresentationProfilesPath() -> QString {
  const QFileInfo info(getConfigPath());
  return info.absoluteDir().absoluteFilePath("presentation_profiles.json");
}

auto SettingsUtils::tightenConfigPermissions() -> void {
  const QString path = getConfigPath();
  if (!QFile::exists(path)) {
    return;
  }
  if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::info(ErrorUtils::ErrorCode::FileWriteError,
                                       "Failed to tighten kartend.cfg permissions to 0600",
                                       "SettingsUtils::tightenConfigPermissions")
            .withDetails(QString("Path: %1").arg(path)));
  }
}

auto SettingsUtils::expandConfigVariables(const QString &input, const QString &collectionName)
    -> QString {
  return PathUtils::validateAndExpandPath(input, collectionName);
}

void SettingsUtils::applyHorizontalScrollbarSetting(QScrollArea *itemScrollArea,
                                                    int collectionIndex,
                                                    const QList<CollectionConfig> &collections) {
  if ((!itemScrollArea) || collectionIndex < 0 || collectionIndex >= collections.size()) {
    return;
  }
  const CollectionConfig &collection = collections[collectionIndex];
  if (collection.gridLayout.hideHorizontalScrollbar) {
    itemScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  } else {
    itemScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

void SettingsUtils::applyVerticalScrollbarSetting(QScrollArea *itemScrollArea, int collectionIndex,
                                                  const QList<CollectionConfig> &collections) {
  if ((!itemScrollArea) || collectionIndex < 0 || collectionIndex >= collections.size()) {
    return;
  }
  const CollectionConfig &collection = collections[collectionIndex];
  if (collection.gridLayout.hideVerticalScrollbar) {
    itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  } else {
    itemScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  }
}

auto SettingsUtils::exportConfig(const QString &destPath) -> ErrorUtils::Result<void> {
  if (destPath.trimmed().isEmpty()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           "Destination path is empty",
                                           "SettingsUtils::exportConfig");
  }

  const QString sourcePath = getConfigPath();
  if (!QFile::exists(sourcePath)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileNotFound,
                                           "No configuration file to export",
                                           "SettingsUtils::exportConfig")
        .withDetails(QString("Expected: %1").arg(sourcePath));
  }

  // Flush any pending QSettings writes so the on-disk file reflects current
  // in-memory state before we copy it.
  {
    QSettings live(sourcePath, getFormat());
    live.sync();
  }

  // Atomic copy: read the just-synced source and write it onto the destination
  // via QSaveFile + parent-dir fsync (Kartend-g2ox).
  auto result = atomicReplaceFile(sourcePath, destPath, "SettingsUtils::exportConfig");
  if (result.isError()) {
    return result;
  }
  // The config may contain plaintext scraper credentials (keychain-unavailable
  // fallback), so tighten the exported copy to 0600 — mirrors importConfig and
  // tightenConfigPermissions. Best-effort: a destination filesystem without
  // POSIX permissions (FAT/exFAT) shouldn't fail an otherwise-successful
  // export, so we log rather than error (Kartend-igcgn).
  if (!QFile::setPermissions(destPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::info(ErrorUtils::ErrorCode::FileWriteError,
                                       "Failed to tighten exported config permissions to 0600",
                                       "SettingsUtils::exportConfig")
            .withDetails(QString("Destination: %1").arg(destPath)));
  }
  return result;
}

auto SettingsUtils::importConfig(const QString &sourcePath) -> ErrorUtils::Result<void> {
  if (sourcePath.trimmed().isEmpty()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           "Source path is empty", "SettingsUtils::importConfig");
  }
  if (!QFile::exists(sourcePath)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileNotFound,
                                           "Selected file does not exist",
                                           "SettingsUtils::importConfig")
        .withDetails(QString("Path: %1").arg(sourcePath));
  }

  // Validate that the file parses as a Kartend INI and contains a [General]
  // group — guards against the user picking an unrelated text file.
  {
    QSettings probe(sourcePath, getFormat());
    if (probe.status() != QSettings::NoError) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::ConfigLoadFailed,
                                             "Selected file is not a valid Kartend config",
                                             "SettingsUtils::importConfig")
          .withDetails(QString("Path: %1, status: %2")
                           .arg(sourcePath)
                           .arg(static_cast<int>(probe.status())));
    }
    if (!probe.childGroups().contains(keys::kGroupGeneral)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::ConfigLoadFailed,
                                             "Selected file is missing the [General] section",
                                             "SettingsUtils::importConfig")
          .withDetails(QString("Path: %1").arg(sourcePath));
    }
  }

  const QString livePath = getConfigPath();
  // Ensure the parent directory exists (getConfigPath creates it, but be
  // defensive in case of unusual filesystem state).
  QDir liveDir = QFileInfo(livePath).absoluteDir();
  if (!liveDir.exists() && !liveDir.mkpath(".")) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::ConfigSaveFailed,
                                           "Failed to create config directory",
                                           "SettingsUtils::importConfig")
        .withDetails(QString("Path: %1").arg(liveDir.absolutePath()));
  }

  // Back up the existing config alongside the live file so a user can recover
  // by hand if the imported settings turn out to be wrong.
  if (QFile::exists(livePath)) {
    const QString backupPath = livePath + ".bak";
    if (QFile::exists(backupPath)) {
      QFile::remove(backupPath);
    }
    if (!QFile::copy(livePath, backupPath)) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                            "Failed to back up existing config before import",
                                            "SettingsUtils::importConfig")
              .withDetails(QString("From: %1, To: %2").arg(livePath, backupPath)));
      // Continue: the import is still atomic via temp+rename, and refusing to
      // proceed would leave the user stuck if .bak is unwritable for some
      // unrelated reason.
    }
  }

  // Atomic replace: install the source over the live config via QSaveFile +
  // parent-dir fsync, so an interrupted import can't leave the live config
  // missing or truncated (Kartend-g2ox).
  if (auto result = atomicReplaceFile(sourcePath, livePath, "SettingsUtils::importConfig");
      result.isError()) {
    return result;
  }
  // QSaveFile creates the new file with umask permissions (not the source's);
  // clamp to 0600 so cleartext scraper credentials don't leak via an
  // over-permissive import.
  tightenConfigPermissions();
  return ErrorUtils::Result<void>::success();
}
