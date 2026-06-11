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
#include <QTextStream>

#include "pathutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

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
  {
    QTextStream out(&file);
    // Extended-M3U marker on line 1 so parsers that look for it (most modern
    // players) recognise the dialect. Each entry gets an EXTINF line with
    // duration -1 (unknown) and the file's basename as the title — we don't
    // currently track per-item titles in playlist_items, but the basename is
    // what every other Kartend surface defaults to.
    out << "#EXTM3U\n";
    for (const PlaylistItemRef &item : items) {
      const QString title = QFileInfo(item.sourcePath).completeBaseName();
      out << "#EXTINF:-1," << title << "\n";
      out << item.sourcePath << "\n";
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
  return items.size();
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
  if (root.value("kartend_playlist_version").toInt(0) < 1) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Missing or unsupported kartend_playlist_version",
                               "PlaylistSerializer::parseJsonFile");
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

  const QJsonArray itemsArray = root.value("items").toArray();
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
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#')) {
      continue;
    }
    paths.append(line);
  }
  file.close();
  return paths;
}

} // namespace PlaylistSerializer
