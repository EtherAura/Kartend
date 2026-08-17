#include "kartwriter.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>

#include "extensionutils.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "kartcompression.h"
#include "kartformat.h"
#include "pathutils.h"

namespace KartWriter {

namespace {

// Kartend-fh3ab: filename-safe rendering of an artwork_type for the
// in-bundle payload path. Only the FILENAME is sanitized — the manifest
// carries the exact type string. The output must pass the reader's
// isSegmentSafe: lowercase [a-z0-9_-] only, and a stem that lands on a
// Windows reserved device name gets a prefix so the bundle this writer
// produces is one the reader will accept.
QString artworkTypeFileStem(const QString &type) {
  QString stem;
  stem.reserve(type.size());
  for (const QChar &c : type) {
    const QChar low = c.toLower();
    const char16_t u = low.unicode();
    const bool keep =
        (u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9') || u == u'-' || u == u'_';
    stem.append(keep ? low : QLatin1Char('_'));
  }
  if (stem.isEmpty()) {
    stem = QStringLiteral("type");
  }
  static const QSet<QString> kReserved = {"con", "prn", "aux", "nul"};
  const bool reservedComLpt =
      stem.size() == 4 &&
      (stem.startsWith(QLatin1String("com")) || stem.startsWith(QLatin1String("lpt"))) &&
      stem.at(3) >= QLatin1Char('1') && stem.at(3) <= QLatin1Char('9');
  if (kReserved.contains(stem) || reservedComLpt) {
    stem.prepend(QStringLiteral("t_"));
  }
  return stem;
}

const QSet<QString> &compressedExtensions() {
  static const QSet<QString> set = {
      "jpg", "jpeg", "png",  "gif", "webp", "avif", "mp4", "mkv", "webm", "mov", "avi",
      "mp3", "ogg",  "opus", "m4a", "flac", "zip",  "7z",  "gz",  "bz2",  "xz",  "zst",
      "rar", "tgz",  "tbz",  "txz", "iso",  "chd",  "cso", "rvz", "wbfs", "nsp", "xci"};
  return set;
}

void writeU8(QDataStream &ds, quint8 v) {
  ds << v;
}
void writeU16(QDataStream &ds, quint16 v) {
  ds << v;
}
void writeU32(QDataStream &ds, quint32 v) {
  ds << v;
}
void writeU64(QDataStream &ds, quint64 v) {
  ds << v;
}

// Buffered entry writer, kept for zlib entries only: qCompress has no
// streaming form short of linking zlib directly, and zlib is only the
// fallback algorithm on builds without zstd. Stored and zstd entries go
// through writeEntryStreamed below in O(chunk) memory.
ErrorUtils::Result<void> writeEntry(QDataStream &ds, const QString &relPath, quint8 flags,
                                    KartFormat::Compression algo, const QByteArray &raw) {
  const QByteArray pathBytes = relPath.toUtf8();
  if (pathBytes.size() == 0 || pathBytes.size() > KartFormat::MAX_PATH_LEN) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           "Entry path empty or too long", "KartWriter::writeEntry")
        .withDetails(relPath);
  }

  const QByteArray sha = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);

  QByteArray payload;
  KartFormat::Compression actual = algo;
  if (algo == KartFormat::Compression_None) {
    payload = raw;
  } else {
    auto res = KartCompression::compress(raw, algo);
    if (res.isError()) return res.error();
    if (res.value().size() < raw.size()) {
      payload = res.value();
    } else {
      payload = raw;
      actual = KartFormat::Compression_None;
    }
  }

  writeU8(ds, flags);
  writeU8(ds, static_cast<quint8>(actual));
  writeU16(ds, static_cast<quint16>(pathBytes.size()));
  ds.writeRawData(pathBytes.constData(), pathBytes.size());
  writeU64(ds, static_cast<quint64>(raw.size()));
  writeU64(ds, static_cast<quint64>(payload.size()));
  ds.writeRawData(sha.constData(), KartFormat::SHA256_SIZE);
  ds.writeRawData(payload.constData(), payload.size());

  if (ds.status() != QDataStream::Ok) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to write Kart entry", "KartWriter::writeEntry")
        .withDetails(relPath);
  }
  return {};
}

// Streamed entry writer for Compression_None / Compression_Zstd. The entry
// header precedes its payload but carries the payload size and content hash,
// which are only known once the payload has been produced — so the header is
// written with placeholder payload_size / sha fields, the payload is streamed
// through the compressor in STREAM_CHUNK_SIZE slices (hashing as it goes),
// and the placeholders are backpatched via seek() once the totals are known.
// QSaveFile's temp file is an ordinary seekable QFileDevice before commit(),
// so this keeps peak memory at O(chunk) instead of O(entry) without touching
// the on-disk byte layout.
ErrorUtils::Result<void> writeEntryStreamed(QSaveFile &out, QDataStream &ds, const QString &relPath,
                                            quint8 flags, KartFormat::Compression algo, QFile &in,
                                            const QAtomicInt &cancel) {
  const QByteArray pathBytes = relPath.toUtf8();
  if (pathBytes.size() == 0 || pathBytes.size() > KartFormat::MAX_PATH_LEN) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           "Entry path empty or too long",
                                           "KartWriter::writeEntryStreamed")
        .withDetails(relPath);
  }
  const quint64 rawSize = static_cast<quint64>(in.size());

  writeU8(ds, flags);
  const qint64 algoOffset = out.pos();
  writeU8(ds, static_cast<quint8>(algo)); // backpatched to None on fallback below
  writeU16(ds, static_cast<quint16>(pathBytes.size()));
  ds.writeRawData(pathBytes.constData(), pathBytes.size());
  writeU64(ds, rawSize);
  const qint64 payloadSizeOffset = out.pos();
  writeU64(ds, 0); // payload_size — backpatched below
  const QByteArray shaPlaceholder(KartFormat::SHA256_SIZE, '\0');
  ds.writeRawData(shaPlaceholder.constData(), shaPlaceholder.size()); // backpatched below
  const qint64 payloadStart = out.pos();

  QCryptographicHash hash(QCryptographicHash::Sha256);
  quint64 payloadSize = 0;
  KartFormat::Compression actual = algo;

  // Streams the source verbatim into the payload region, hashing as it goes.
  // Used for Compression_None entries and for the incompressible-fallback
  // rewrite; the rewind + hash.reset() make the second pass self-contained so
  // a source file mutating between passes yields a fresh, consistent hash.
  auto streamRaw = [&]() -> ErrorUtils::Result<void> {
    if (!in.seek(0)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                             "Cannot rewind source file",
                                             "KartWriter::writeEntryStreamed")
          .withDetails(in.errorString() + " path=" + in.fileName());
    }
    hash.reset();
    quint64 total = 0;
    while (!in.atEnd()) {
      if (cancel.loadRelaxed()) {
        return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::OperationCancelled,
                                               "Kart write cancelled",
                                               "KartWriter::writeEntryStreamed");
      }
      const QByteArray chunk = in.read(KartCompression::STREAM_CHUNK_SIZE);
      if (chunk.isEmpty()) {
        return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                               "Read error while streaming source file",
                                               "KartWriter::writeEntryStreamed")
            .withDetails(in.errorString() + " path=" + in.fileName());
      }
      hash.addData(chunk);
      ds.writeRawData(chunk.constData(), chunk.size());
      total += static_cast<quint64>(chunk.size());
    }
    if (total != rawSize) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                             "Source file size changed during export",
                                             "KartWriter::writeEntryStreamed")
          .withDetails(
              QString("expected=%1 got=%2 path=%3").arg(rawSize).arg(total).arg(in.fileName()));
    }
    return {};
  };

  if (algo == KartFormat::Compression_None) {
    if (auto r = streamRaw(); r.isError()) {
      return r.error();
    }
    payloadSize = rawSize;
  } else {
    // Compression_Zstd (zlib entries take the buffered writeEntry path).
    KartCompression::StreamCompressor comp;
    if (auto beginRes = comp.begin(rawSize); beginRes.isError()) {
      return beginRes.error();
    }
    quint64 compressedWritten = 0;
    while (!in.atEnd()) {
      if (cancel.loadRelaxed()) {
        return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::OperationCancelled,
                                               "Kart write cancelled",
                                               "KartWriter::writeEntryStreamed");
      }
      const QByteArray chunk = in.read(KartCompression::STREAM_CHUNK_SIZE);
      if (chunk.isEmpty()) {
        return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                               "Read error while streaming source file",
                                               "KartWriter::writeEntryStreamed")
            .withDetails(in.errorString() + " path=" + in.fileName());
      }
      hash.addData(chunk);
      auto compressed = comp.compress(chunk);
      if (compressed.isError()) {
        return compressed.error();
      }
      if (!compressed.value().isEmpty()) {
        ds.writeRawData(compressed.value().constData(), compressed.value().size());
        compressedWritten += static_cast<quint64>(compressed.value().size());
      }
    }
    auto tail = comp.finish();
    if (tail.isError()) {
      return tail.error();
    }
    if (!tail.value().isEmpty()) {
      ds.writeRawData(tail.value().constData(), tail.value().size());
      compressedWritten += static_cast<quint64>(tail.value().size());
    }

    if (compressedWritten < rawSize) {
      payloadSize = compressedWritten;
    } else {
      // Compression grew (or merely matched) the payload — store the entry
      // uncompressed instead, exactly like the buffered writer's size
      // comparison. Rewrite the payload region with the source bytes and drop
      // the compressed excess.
      if (!out.seek(payloadStart)) {
        return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                               "Seek failed while rewriting Kart entry payload",
                                               "KartWriter::writeEntryStreamed")
            .withDetails(relPath);
      }
      if (auto r = streamRaw(); r.isError()) {
        return r.error();
      }
      actual = KartFormat::Compression_None;
      payloadSize = rawSize;
      const qint64 endAfterRaw = payloadStart + static_cast<qint64>(rawSize);
      if (out.size() > endAfterRaw && !out.resize(endAfterRaw)) {
        return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                               "Cannot truncate Kart entry payload",
                                               "KartWriter::writeEntryStreamed")
            .withDetails(out.errorString() + " " + relPath);
      }
    }
  }

  // Backpatch the placeholder payload_size / sha fields (and the compression
  // byte when the fallback stored the entry raw), then restore the stream
  // position for the next entry.
  const QByteArray sha = hash.result();
  const qint64 endPos = out.pos();
  if (!out.seek(payloadSizeOffset)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Seek failed while backpatching Kart entry header",
                                           "KartWriter::writeEntryStreamed")
        .withDetails(relPath);
  }
  writeU64(ds, payloadSize);
  ds.writeRawData(sha.constData(), KartFormat::SHA256_SIZE);
  if (actual != algo) {
    if (!out.seek(algoOffset)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                             "Seek failed while backpatching Kart entry header",
                                             "KartWriter::writeEntryStreamed")
          .withDetails(relPath);
    }
    writeU8(ds, static_cast<quint8>(actual));
  }
  if (!out.seek(endPos)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Seek failed while backpatching Kart entry header",
                                           "KartWriter::writeEntryStreamed")
        .withDetails(relPath);
  }

  if (ds.status() != QDataStream::Ok) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to write Kart entry",
                                           "KartWriter::writeEntryStreamed")
        .withDetails(relPath);
  }
  return {};
}

void inlineLauncherPresets(WriterParams &params) {
  QSet<QString> referenced;
  auto collect = [&](const LauncherConfig &lc) {
    if (!lc.presetId.isEmpty()) referenced.insert(lc.presetId);
  };
  if (!params.collectionConfig.launcher.launcherPath.isEmpty() ||
      !params.collectionConfig.launcher.launcherName.isEmpty()) {
    // legacy primary launcher has no preset id
  }
  for (const LauncherConfig &lc : params.collectionConfig.launcher.additionalLaunchers) {
    collect(lc);
  }

  QList<LauncherPreset> kept;
  for (const LauncherPreset &p : std::as_const(params.launchers)) {
    if (referenced.contains(p.id)) {
      kept.append(p);
    }
  }
  params.launchers = kept;
}

} // namespace

bool extensionShouldCompress(const QString &path) {
  const QString ext = QFileInfo(path).suffix().toLower();
  if (ext.isEmpty()) return true;
  return !compressedExtensions().contains(ext);
}

Writer::Writer(QObject *parent) : QObject(parent) {}

ErrorUtils::Result<void> Writer::writeKart(const QString &outPath, const WriterParams &params) {
  // Streams entries through one QSaveFile (commit + syncDirectory below) —
  // deliberately not PathUtils::atomicWriteFile, which takes the whole
  // payload as a single QByteArray: a kart carries every media file, so
  // buffering it in memory is not viable.
  QSaveFile out(outPath);
  if (!out.open(QIODevice::WriteOnly)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Cannot open Kart output file", "KartWriter::writeKart")
        .withDetails(out.errorString());
  }
  QDataStream ds(&out);
  ds.setByteOrder(QDataStream::LittleEndian);

  ds.writeRawData(KartFormat::MAGIC.data(), KartFormat::MAGIC_SIZE);

  KartManifest::Manifest manifest;
  manifest.formatVersion = KartFormat::CURRENT_VERSION;
  manifest.uuid = params.uuid;
  manifest.version = params.version;
  manifest.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  manifest.name = params.name;
  manifest.author = params.author;
  manifest.description = params.description;
  manifest.license = params.license;
  manifest.collectionConfig = params.collectionConfig;
  manifest.launchers = params.launchers;
  for (const ItemSource &item : params.items) {
    manifest.items.append(item.manifestItem);
  }
  manifest.playlists = params.playlists;
  const QByteArray manifestJson = KartManifest::serialize(manifest);
  if (static_cast<quint64>(manifestJson.size()) > KartFormat::MAX_MANIFEST_SIZE) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartManifestInvalid,
                                           "Serialized manifest exceeds maximum size",
                                           "KartWriter::writeKart");
  }
  writeU32(ds, static_cast<quint32>(manifestJson.size()));
  ds.writeRawData(manifestJson.constData(), manifestJson.size());

  const int totalEntries = [&]() {
    int n = 0;
    for (const ItemSource &it : params.items) {
      if (!it.mediaAbs.isEmpty()) ++n;
      if (!it.artworkAbs.isEmpty()) ++n;
      if (!it.videoAbs.isEmpty()) ++n;
      if (!it.manualAbs.isEmpty()) ++n;
      n += static_cast<int>(it.artworkLinkAbs.size());
    }
    return n;
  }();
  int written = 0;

  auto emitProgress = [&]() {
    if (totalEntries > 0) {
      emit progress(static_cast<double>(written) / static_cast<double>(totalEntries));
    }
  };

  auto writeOne = [&](const QString &absPath, const QString &relPath,
                      quint8 flags) -> ErrorUtils::Result<void> {
    if (m_cancel.loadRelaxed()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::OperationCancelled,
                                             "Kart write cancelled", "KartWriter::writeKart");
    }
    QFile in(absPath);
    if (!in.open(QIODevice::ReadOnly)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                             "Cannot open source file for reading",
                                             "KartWriter::writeKart")
          .withDetails(in.errorString() + " path=" + absPath);
    }
    if (in.size() < 0 || static_cast<quint64>(in.size()) > KartFormat::MAX_ENTRY_SIZE) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                             "Source file exceeds Kart entry size limit",
                                             "KartWriter::writeKart")
          .withDetails(QString("size=%1 max=%2").arg(in.size()).arg(KartFormat::MAX_ENTRY_SIZE));
    }
    const KartFormat::Compression algo = extensionShouldCompress(absPath)
                                             ? params.preferredCompression
                                             : KartFormat::Compression_None;
    // Zlib entries (the no-zstd fallback) keep the buffered path — qCompress
    // has no streaming form without a direct zlib dependency; stored and zstd
    // entries stream in O(chunk) memory.
    auto entryRes = (algo == KartFormat::Compression_Zlib)
                        ? writeEntry(ds, relPath, flags, algo, in.readAll())
                        : writeEntryStreamed(out, ds, relPath, flags, algo, in, m_cancel);
    if (entryRes.isError()) return entryRes.error();
    ++written;
    emitProgress();
    return {};
  };

  for (const ItemSource &item : params.items) {
    if (!item.mediaAbs.isEmpty() && !item.manifestItem.mediaPath.isEmpty()) {
      auto r = writeOne(item.mediaAbs, item.manifestItem.mediaPath, KartFormat::Flag_Media);
      if (r.isError()) return r.error();
    }
    if (!item.artworkAbs.isEmpty() && !item.manifestItem.artworkPath.isEmpty()) {
      auto r = writeOne(item.artworkAbs, item.manifestItem.artworkPath, KartFormat::Flag_Artwork);
      if (r.isError()) return r.error();
    }
    if (!item.videoAbs.isEmpty() && !item.manifestItem.videoPath.isEmpty()) {
      auto r = writeOne(item.videoAbs, item.manifestItem.videoPath, KartFormat::Flag_Video);
      if (r.isError()) return r.error();
    }
    if (!item.manualAbs.isEmpty() && !item.manifestItem.manualPath.isEmpty()) {
      auto r = writeOne(item.manualAbs, item.manifestItem.manualPath, KartFormat::Flag_Manual);
      if (r.isError()) return r.error();
    }
    // Kartend-fh3ab: hand-linked artwork payloads, index-aligned with the
    // manifest's artworkLinks (prepareFromCollection builds both together).
    const qsizetype linkCount =
        qMin(item.artworkLinkAbs.size(), item.manifestItem.artworkLinks.size());
    for (qsizetype li = 0; li < linkCount; ++li) {
      auto r = writeOne(item.artworkLinkAbs.at(li), item.manifestItem.artworkLinks.at(li).path,
                        KartFormat::Flag_Artwork);
      if (r.isError()) return r.error();
    }
  }

  if (!out.commit()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to commit Kart output file",
                                           "KartWriter::writeKart")
        .withDetails(out.errorString());
  }
  PathUtils::syncDirectory(QFileInfo(outPath).absolutePath());
  return {};
}

ErrorUtils::Result<WriterParams> prepareFromCollection(const CollectionConfig &cfg,
                                                       const QString &collectionUuid,
                                                       const QList<LauncherPreset> &allPresets,
                                                       QSqlDatabase *db) {
  WriterParams params;
  params.collectionConfig = cfg;
  params.name = cfg.name;
  params.uuid = collectionUuid;
  params.launchers = allPresets;
  params.collectionConfig.parentCollectionIndex = -1;
  params.collectionConfig.isSubcollection = false;
  params.collectionConfig.additionalParentNames.clear();

  inlineLauncherPresets(params);

  QDir mediaDir(cfg.mediaDirectory);
  if (cfg.mediaDirectory.isEmpty() || !mediaDir.exists()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::MediaDirectoryNotFound,
                                           "Collection media directory missing",
                                           "KartWriter::prepareFromCollection")
        .withDetails(cfg.mediaDirectory);
  }
  QStringList nameFilters = ExtensionUtils::toNameFilters(cfg.extensions);
  if (nameFilters.isEmpty()) {
    nameFilters << "*.*";
  }
  const QFileInfoList mediaFiles =
      mediaDir.entryInfoList(nameFilters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

  QDir artworkDir(cfg.artworkDirectory);
  QDir videoDir(cfg.videoDirectory);
  QDir manualDir(cfg.manualDirectory);

  auto findSibling = [](const QDir &dir, const QString &baseName,
                        const QStringList &exts) -> QString {
    if (!dir.exists()) return QString();
    for (const QString &ext : exts) {
      const QString cand = dir.filePath(baseName + "." + ext);
      if (QFileInfo::exists(cand)) return cand;
    }
    return QString();
  };

  // Shared extension tables (ExtensionUtils) so the exporter bundles every
  // sibling asset the in-app artwork/video lookup would find. The old local
  // lists silently skipped .bmp/.avif artwork and .avi videos that the grid
  // and details pane display fine — exporting then quietly dropped them.
  const QStringList &kArtworkExts = ExtensionUtils::imageBaseExtensions();
  const QStringList &kVideoExts = ExtensionUtils::videoBaseExtensions();

  for (const QFileInfo &mediaInfo : mediaFiles) {
    ItemSource item;
    item.mediaAbs = mediaInfo.absoluteFilePath();
    const QString base = mediaInfo.completeBaseName();
    const QString fileName = mediaInfo.fileName();
    item.manifestItem.mediaPath = "media/" + fileName;
    item.manifestItem.title = base;

    if (artworkDir.exists()) {
      const QString aw = findSibling(artworkDir, base, kArtworkExts);
      if (!aw.isEmpty()) {
        item.artworkAbs = aw;
        item.manifestItem.artworkPath = "artwork/" + QFileInfo(aw).fileName();
      }
    }
    if (videoDir.exists()) {
      const QString v = findSibling(videoDir, base, kVideoExts);
      if (!v.isEmpty()) {
        item.videoAbs = v;
        item.manifestItem.videoPath = "video/" + QFileInfo(v).fileName();
      }
    }
    if (manualDir.exists()) {
      const QString m = ItemMetadataStore::findManualForBaseName(base, manualDir.absolutePath());
      if (!m.isEmpty()) {
        item.manualAbs = m;
        item.manifestItem.manualPath = "manual/" + QFileInfo(m).fileName();
      }
    }

    if (db && db->isOpen()) {
      auto load = ItemMetadataStore::load(*db, collectionUuid, item.mediaAbs);
      if (load.isOk()) {
        item.manifestItem.metadata = load.value();
        item.manifestItem.metadata.collectionUuid.clear();
        item.manifestItem.metadata.path.clear();
        item.manifestItem.metadata.updatedAt.clear();
        item.manifestItem.launcherIndex = item.manifestItem.metadata.launcherIndex;
      }

      // Kartend-fh3ab: bundle the item's hand-linked artwork. The sibling
      // scan above only finds name-matched files under artworkDirectory; an
      // item_artwork manual link can point anywhere on disk, and those files
      // are exactly what a backup must carry. Payloads are laid out
      // item_artwork/<item index>/<type>.<ext> — the index namespaces items
      // (two items may link files with colliding names), the manifest keeps
      // the exact type string, and only the filename is sanitized. A dead
      // link (file since deleted) is skipped, matching how the UI resolves
      // it to nothing rather than erroring.
      auto links = ItemArtworkStore::loadAllForItem(*db, collectionUuid, item.mediaAbs);
      if (links.isOk()) {
        QSet<QString> usedNames;
        for (const ItemArtworkStore::ItemArtwork &row : links.value()) {
          if (item.manifestItem.artworkLinks.size() >=
              KartFormat::MAX_MANIFEST_ARTWORK_LINKS_PER_ITEM) {
            break; // reader-enforced ceiling — never write a bundle it rejects
          }
          if (row.manualPath.isEmpty()) continue;
          const QString abs = PathUtils::expandPathWithoutExistenceCheck(row.manualPath);
          if (!QFileInfo::exists(abs)) continue;
          const QString suffix = QFileInfo(abs).suffix().toLower();
          // "payloadBase", not "base" — the enclosing item loop already
          // binds `base` to the item's basename (-Wshadow is fatal on the
          // Release/maintenance legs).
          const QString payloadBase = artworkTypeFileStem(row.artworkType) +
                                      (suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix);
          // Distinct types can sanitize to the same filename; bump until
          // free so the extractor's duplicate-entry guard never trips on a
          // bundle this writer produced.
          QString name = payloadBase;
          int bump = 2;
          while (usedNames.contains(name)) {
            name = QString::number(bump++) + QStringLiteral("-") + payloadBase;
          }
          usedNames.insert(name);
          KartManifest::ArtworkLink link;
          link.type = row.artworkType;
          link.path = QStringLiteral("item_artwork/%1/%2").arg(params.items.size()).arg(name);
          item.manifestItem.artworkLinks.append(link);
          item.artworkLinkAbs.append(abs);
        }
      }
    }

    params.items.append(item);
  }

  return params;
}

} // namespace KartWriter
