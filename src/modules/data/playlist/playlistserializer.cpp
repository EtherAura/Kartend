// Playlist JSON / M3U serializers, moved verbatim from playlistmanager.cpp
// (Kartend-k4alv). Pure format + file I/O — the DB halves (row/item loads,
// CRUD, transactional inserts, path resolution) stay on PlaylistManager.

#include "playlistserializer.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>

#include "errorutils.h"
#include "pathutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace {

// Highest on-disk envelope version this build knows how to read. Bumped in
// lockstep with the version written by exportJson (currently 2). parseJsonFile
// rejects anything above this so a file produced by a newer Kartend is refused
// outright rather than parsed against a shape this build doesn't understand
// (Kartend-2sg2t).
constexpr int KARTEND_PLAYLIST_MAX_VERSION = 2;

} // namespace

namespace PlaylistSerializer {

ErrorUtils::Result<int> exportJson(const PlaylistRow &row, const QList<PlaylistItemRef> &items,
                                   const QString &outPath) {
  QJsonArray itemsArray;
  for (const PlaylistItemRef &item : items) {
    QJsonObject obj;
    obj["source_collection_uuid"] = item.sourceCollectionUuid;
    obj["source_path"] = item.sourcePath;
    obj["added_at"] = item.addedAt;
    itemsArray.append(obj);
  }

  QJsonObject doc;
  // Versioned wrapper so a future schema change can branch on read without
  // having to sniff fields. Bump on any breaking change to the item shape.
  // v2 added the is_smart / smart_filter pair; importers should fall back
  // to the v1 shape when those fields are absent.
  doc["kartend_playlist_version"] = 2;
  doc["name"] = row.name;
  doc["icon"] = row.icon;
  doc["parent_collection_uuid"] = row.parentCollectionUuid;
  // reserved_kind intentionally omitted — exporting "favorites" and re-
  // importing would create a duplicate reserved row, which deletePlaylist
  // refuses to clean up. Imported playlists are always plain user playlists.
  doc["created_at"] = row.createdAt;
  doc["updated_at"] = row.updatedAt;
  // Smart playlists carry their filter spec; static playlists carry their
  // explicit items list. Both are emitted unconditionally so a downstream
  // tool that doesn't know the version field can still round-trip the
  // shape it cares about.
  doc["is_smart"] = row.isSmart;
  doc["smart_filter"] = row.smartFilterJson;
  doc["items"] = itemsArray;

  QSaveFile file(outPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to open output file",
                               "PlaylistSerializer::exportJson")
        .withDetails(file.errorString());
  }
  const QByteArray bytes = QJsonDocument(doc).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size()) {
    file.cancelWriting();
    return ErrorContext::error(ErrorCode::FileWriteError, "Short write to JSON file",
                               "PlaylistSerializer::exportJson");
  }
  if (!file.commit()) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to commit JSON file",
                               "PlaylistSerializer::exportJson")
        .withDetails(file.errorString());
  }
  PathUtils::syncDirectory(QFileInfo(outPath).absolutePath());
  return items.size();
}

ErrorUtils::Result<int> exportM3U(const QList<PlaylistItemRef> &items, const QString &outPath) {
  QSaveFile file(outPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to open output file",
                               "PlaylistSerializer::exportM3U")
        .withDetails(file.errorString());
  }
  int written = 0;
  {
    QTextStream out(&file);
    // Pin UTF-8 so round-trip fidelity is deterministic across locales /
    // platforms rather than depending on Qt6's implicit default encoding
    // (Kartend-93ju5).
    out.setEncoding(QStringConverter::Utf8);
    // Extended-M3U marker on line 1 so parsers that look for it (most modern
    // players) recognise the dialect. Each entry gets an EXTINF line with
    // duration -1 (unknown) and the file's basename as the title — we don't
    // currently track per-item titles in playlist_items, but the basename is
    // what every other Kartend surface defaults to.
    out << "#EXTM3U\n";
    for (const PlaylistItemRef &item : items) {
      // A path containing a newline or carriage return would split one logical
      // entry across multiple M3U lines; on re-read the continuation would parse
      // as a separate (garbage) path and corrupt the file. M3U has no escaping
      // for embedded line breaks, so skip the entry and warn rather than emit a
      // file that round-trips into corruption (Kartend-93ju5).
      if (item.sourcePath.contains(QLatin1Char('\n')) ||
          item.sourcePath.contains(QLatin1Char('\r'))) {
        ErrorUtils::logError(
            ErrorContext::warning(ErrorCode::InvalidArgument,
                                  "M3U export: skipping path containing a newline/carriage return "
                                  "(would corrupt the file)",
                                  "PlaylistSerializer::exportM3U")
                .withDetails(item.sourcePath));
        continue;
      }
      const QString title = QFileInfo(item.sourcePath).completeBaseName();
      out << "#EXTINF:-1," << title << "\n";
      out << item.sourcePath << "\n";
      ++written;
    }
    out.flush();
    if (out.status() != QTextStream::Ok) {
      file.cancelWriting();
      return ErrorContext::error(ErrorCode::FileWriteError, "Failed to write M3U content",
                                 "PlaylistSerializer::exportM3U");
    }
  }
  if (!file.commit()) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Failed to commit M3U file",
                               "PlaylistSerializer::exportM3U")
        .withDetails(file.errorString());
  }
  PathUtils::syncDirectory(QFileInfo(outPath).absolutePath());
  return written;
}

ErrorUtils::Result<ParsedPlaylist> parseJsonFile(const QString &inPath) {
  QFile file(inPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Failed to open input file",
                               "PlaylistSerializer::parseJsonFile")
        .withDetails(file.errorString());
  }
  const QByteArray raw = file.readAll();
  file.close();

  QJsonParseError parseErr;
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseErr);
  if (doc.isNull() || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Failed to parse JSON",
                               "PlaylistSerializer::parseJsonFile")
        .withDetails(parseErr.errorString());
  }
  const QJsonObject root = doc.object();
  // Read the version as a raw double first so a non-integral value like 2.9
  // does not silently truncate into the accepted range (QJsonValue::toInt only
  // returns the integer when the stored double is exactly integral). Reject
  // anything below 1 (missing/garbage) or above the highest version this build
  // knows how to read (Kartend-2sg2t).
  const double versionRaw = root.value("kartend_playlist_version").toDouble(0.0);
  const int version = root.value("kartend_playlist_version").toInt(0);
  if (version < 1 || version > KARTEND_PLAYLIST_MAX_VERSION ||
      static_cast<double>(version) != versionRaw) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Missing or unsupported kartend_playlist_version",
                               "PlaylistSerializer::parseJsonFile")
        .withDetails(QStringLiteral("version=%1 (supported range 1..%2)")
                         .arg(versionRaw)
                         .arg(KARTEND_PLAYLIST_MAX_VERSION));
  }

  ParsedPlaylist out;
  out.name = root.value("name").toString();
  // parent_collection_uuid is preserved on a best-effort basis. The
  // synthesizer's resync (MainWindow::resyncPlaylistCollections) falls back
  // to root level when the uuid no longer matches any real collection, so a
  // stale reference is harmless.
  out.parentCollectionUuid = root.value("parent_collection_uuid").toString();
  // v2: smart playlist round-trip. v1 documents never carry these fields so
  // they fall through to the static path with an empty items consumer.
  out.isSmart = root.value("is_smart").toBool(false);
  out.smartFilterJson = root.value("smart_filter").toString();

  // A present "items" field that isn't an array (e.g. an object, a string, or a
  // number) signals a structurally invalid file — previously it silently
  // collapsed to an empty playlist reported as a successful import. A missing or
  // null field is still tolerated (smart playlists and legacy item-less exports
  // legitimately omit it), yielding an empty static item list (Kartend-2sg2t).
  const QJsonValue itemsValue = root.value("items");
  if (!itemsValue.isUndefined() && !itemsValue.isNull() && !itemsValue.isArray()) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Playlist 'items' field is present but not an array",
                               "PlaylistSerializer::parseJsonFile");
  }
  const QJsonArray itemsArray = itemsValue.toArray();
  out.items.reserve(itemsArray.size());
  for (const auto &val : itemsArray) {
    const QJsonObject obj = val.toObject();
    out.items.append(ParsedPlaylist::Item{obj.value("source_collection_uuid").toString(),
                                          obj.value("source_path").toString()});
  }
  return out;
}

ErrorUtils::Result<QStringList> readM3UPaths(const QString &inPath) {
  QFile file(inPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Failed to open input file",
                               "PlaylistSerializer::readM3UPaths")
        .withDetails(file.errorString());
  }
  QStringList paths;
  QTextStream in(&file);
  // Pin UTF-8 so re-reads decode identically to what exportM3U wrote,
  // independent of the platform/locale default encoding (Kartend-93ju5).
  in.setEncoding(QStringConverter::Utf8);
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();
    // KNOWN LIMITATION (Kartend-93ju5): a path that legitimately begins with
    // '#' is indistinguishable from an M3U comment line and is dropped here.
    // The M3U format reserves a leading '#' for directives/comments and offers
    // no standard escaping for paths that start with it, so we deliberately do
    // not invent a non-standard escape scheme; such entries cannot survive an
    // M3U round-trip (use the lossless JSON format for those paths).
    if (line.isEmpty() || line.startsWith('#')) {
      continue;
    }
    paths.append(line);
  }
  file.close();
  return paths;
}

} // namespace PlaylistSerializer
