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

#include "itemmetadata.h"
#include "kartcompression.h"
#include "kartformat.h"

namespace KartWriter {

namespace {

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

ErrorUtils::Result<QByteArray> readFileFully(const QString &path) {
  QFile in(path);
  if (!in.open(QIODevice::ReadOnly)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                           "Cannot open source file for reading",
                                           "KartWriter::readFileFully")
        .withDetails(in.errorString() + " path=" + path);
  }
  if (in.size() < 0 || static_cast<quint64>(in.size()) > KartFormat::MAX_ENTRY_SIZE) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::InvalidArgument,
                                           "Source file exceeds Kart entry size limit",
                                           "KartWriter::readFileFully")
        .withDetails(QString("size=%1 max=%2").arg(in.size()).arg(KartFormat::MAX_ENTRY_SIZE));
  }
  return in.readAll();
}

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

void inlineLauncherPresets(WriterParams &params) {
  QSet<QString> referenced;
  auto collect = [&](const LauncherConfig &lc) {
    if (!lc.presetId.isEmpty()) referenced.insert(lc.presetId);
  };
  if (!params.collectionConfig.launcherPath.isEmpty() ||
      !params.collectionConfig.launcherName.isEmpty()) {
    // legacy primary launcher has no preset id
  }
  for (const LauncherConfig &lc : params.collectionConfig.additionalLaunchers) {
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
    auto raw = readFileFully(absPath);
    if (raw.isError()) return raw.error();
    const KartFormat::Compression algo = extensionShouldCompress(absPath)
                                             ? params.preferredCompression
                                             : KartFormat::Compression_None;
    auto entryRes = writeEntry(ds, relPath, flags, algo, raw.value());
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
  }

  if (!out.commit()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Failed to commit Kart output file",
                                           "KartWriter::writeKart")
        .withDetails(out.errorString());
  }
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
  QStringList nameFilters;
  for (const QString &ext : cfg.extensions) {
    nameFilters.append("*." + ext);
  }
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

  static const QStringList kArtworkExts = {"png", "jpg", "jpeg", "webp", "gif"};
  static const QStringList kVideoExts = {"mp4", "mkv", "webm", "mov"};

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
    }

    params.items.append(item);
  }

  return params;
}

} // namespace KartWriter
