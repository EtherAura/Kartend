#include "kartreader.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <limits>

#include "kartcompression.h"
#include "kartformat.h"
#include "pathutils.h"

namespace KartReader {

namespace {

ErrorUtils::ErrorContext readError(const QString &msg, const QString &details = QString()) {
  auto ctx =
      ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartFormatInvalid, msg, "KartReader");
  if (!details.isEmpty()) {
    ctx.withDetails(details);
  }
  return ctx;
}

ErrorUtils::Result<QByteArray> readExactly(QFile &f, qint64 n) {
  if (n < 0) {
    return readError("Negative read length");
  }
  // Guard the narrowing to qsizetype: on 32-bit targets qsizetype is 32-bit,
  // and callers pass header-declared sizes up to MAX_ENTRY_SIZE (8 GiB).
  // Reject anything that won't fit the buffer's index type rather than
  // truncating it (the prior static_cast<int> could wrap to a tiny/negative
  // size while the read loop was still told the full length).
  if (n > static_cast<qint64>(std::numeric_limits<qsizetype>::max())) {
    return readError("Read length exceeds addressable range", QString("requested=%1").arg(n));
  }
  // Header-declared lengths are untrusted; a length past the bytes remaining
  // in the file can only end in "unexpected end", so reject it before
  // allocating a buffer sized to the declaration.
  const qint64 remaining = f.size() - f.pos();
  if (n > remaining) {
    return readError(
        "Declared length exceeds remaining file size",
        QString("requested=%1 remaining=%2 at offset=%3").arg(n).arg(remaining).arg(f.pos()));
  }
  QByteArray buf;
  buf.resize(static_cast<qsizetype>(n));
  qint64 got = 0;
  while (got < n) {
    const qint64 r = f.read(buf.data() + got, n - got);
    if (r <= 0) {
      return readError("Unexpected end of Kart file",
                       QString("requested=%1 got=%2 at offset=%3").arg(n).arg(got).arg(f.pos()));
    }
    got += r;
  }
  return buf;
}

ErrorUtils::Result<quint32> readU32LE(QFile &f) {
  auto bytes = readExactly(f, 4);
  if (bytes.isError()) return bytes.error();
  QDataStream ds(bytes.value());
  ds.setByteOrder(QDataStream::LittleEndian);
  quint32 v = 0;
  ds >> v;
  return v;
}

ErrorUtils::Result<quint16> readU16LE(QFile &f) {
  auto bytes = readExactly(f, 2);
  if (bytes.isError()) return bytes.error();
  QDataStream ds(bytes.value());
  ds.setByteOrder(QDataStream::LittleEndian);
  quint16 v = 0;
  ds >> v;
  return v;
}

ErrorUtils::Result<quint64> readU64LE(QFile &f) {
  auto bytes = readExactly(f, 8);
  if (bytes.isError()) return bytes.error();
  QDataStream ds(bytes.value());
  ds.setByteOrder(QDataStream::LittleEndian);
  quint64 v = 0;
  ds >> v;
  return v;
}

ErrorUtils::Result<quint8> readU8(QFile &f) {
  auto bytes = readExactly(f, 1);
  if (bytes.isError()) return bytes.error();
  return static_cast<quint8>(bytes.value().at(0));
}

ErrorUtils::Result<void> validateMagic(QFile &f) {
  auto bytes = readExactly(f, KartFormat::MAGIC_SIZE);
  if (bytes.isError()) return bytes.error();
  if (!KartFormat::hasMagic(bytes.value().constData(), bytes.value().size())) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartFormatInvalid,
                                           "File is not a Kart package (bad magic)",
                                           "KartReader::validateMagic");
  }
  return {};
}

ErrorUtils::Result<KartManifest::Manifest> readManifest(QFile &f) {
  auto sizeRes = readU32LE(f);
  if (sizeRes.isError()) return sizeRes.error();
  const quint32 manifestLen = sizeRes.value();
  if (manifestLen == 0 || manifestLen > KartFormat::MAX_MANIFEST_SIZE) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartManifestInvalid,
                                           "Manifest length out of range",
                                           "KartReader::readManifest")
        .withDetails(QString("len=%1 max=%2").arg(manifestLen).arg(KartFormat::MAX_MANIFEST_SIZE));
  }
  auto bytes = readExactly(f, manifestLen);
  if (bytes.isError()) return bytes.error();
  return KartManifest::parse(bytes.value());
}

// Reject segments that alias or collide on Windows. Applied unconditionally —
// like the backslash / drive-letter rejections — so a portable .kart written
// anywhere stays importable everywhere: Windows strips trailing dots and
// spaces from path segments (so "name." collides with "name"), and reserved
// device names resolve to the device regardless of any extension.
bool isSegmentSafe(const QString &seg) {
  if (seg == QLatin1String("..")) return false;
  if (seg.contains('\\')) return false;
  // A trailing dot or space also covers the bare "." segment and segments
  // consisting only of dots and/or spaces — all of which end in one.
  const QChar last = seg.back();
  if (last == QLatin1Char('.') || last == QLatin1Char(' ')) return false;
  // Reserved device names match case-insensitively on the stem before any
  // extension ("CON", "con.txt", "Com1.bin"), with trailing stem spaces
  // ignored the way Windows ignores them.
  const qsizetype dot = seg.indexOf(QLatin1Char('.'));
  QString stem = dot < 0 ? seg : seg.left(dot);
  while (stem.endsWith(QLatin1Char(' '))) {
    stem.chop(1);
  }
  const QString upper = stem.toUpper();
  if (upper == QLatin1String("CON") || upper == QLatin1String("PRN") ||
      upper == QLatin1String("AUX") || upper == QLatin1String("NUL")) {
    return false;
  }
  if (upper.size() == 4 &&
      (upper.startsWith(QLatin1String("COM")) || upper.startsWith(QLatin1String("LPT"))) &&
      upper.at(3) >= QLatin1Char('1') && upper.at(3) <= QLatin1Char('9')) {
    return false;
  }
  return true;
}

bool isPathSafe(const QString &relPath) {
  if (relPath.isEmpty()) return false;
  if (relPath.contains(QChar('\0'))) return false;
  if (relPath.startsWith('/') || relPath.startsWith('\\')) return false;
  if (relPath.size() >= 2 && relPath.at(1) == QChar(':')) return false;
  const QStringList segments = relPath.split('/', Qt::SkipEmptyParts);
  for (const QString &seg : segments) {
    if (!isSegmentSafe(seg)) return false;
  }
  return true;
}

} // namespace

ErrorUtils::Result<KartManifest::Manifest> peekManifest(const QString &kartPath) {
  QFile f(kartPath);
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                           "Cannot open Kart file for reading",
                                           "KartReader::peekManifest")
        .withDetails(f.errorString());
  }
  auto magicRes = validateMagic(f);
  if (magicRes.isError()) return magicRes.error();
  return readManifest(f);
}

Extractor::Extractor(QObject *parent) : QObject(parent) {}

ErrorUtils::Result<ExtractResult> Extractor::extractTo(const QString &kartPath,
                                                       const QString &destDir) {
  QFile f(kartPath);
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileReadError,
                                           "Cannot open Kart file for reading",
                                           "KartReader::extractTo")
        .withDetails(f.errorString());
  }
  const qint64 totalSize = f.size();

  if (auto magicRes = validateMagic(f); magicRes.isError()) {
    return magicRes.error();
  }
  auto manifestRes = readManifest(f);
  if (manifestRes.isError()) return manifestRes.error();

  QDir dest(destDir);
  if (!dest.exists() && !dest.mkpath(".")) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                           "Cannot create destination directory",
                                           "KartReader::extractTo")
        .withDetails(destDir);
  }
  const QString destAbs = QFileInfo(destDir).absoluteFilePath();

  ExtractResult result;
  result.manifest = manifestRes.value();
  result.destDir = destAbs;

  QSet<QString> dirsToSync;

  // Aggregate decompression-bomb guards (Kartend-4e8ku). The per-entry checks
  // below bound each entry individually, but a crafted bundle can still exhaust
  // inodes (millions of tiny entries) or disk (many near-max entries) in
  // aggregate. Track entry count and cumulative written bytes and abort cleanly
  // once either crosses its ceiling, mirroring archiverepack's kMaxEntries and
  // launchmanagerarchive's cumulative MAX_EXTRACTION_BYTES.
  quint64 entryCount = 0;
  quint64 totalExtractedBytes = 0;

  while (!f.atEnd()) {
    if (m_cancel.loadRelaxed()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::OperationCancelled,
                                             "Kart extraction cancelled", "KartReader::extractTo");
    }

    if (++entryCount > KartFormat::MAX_ENTRY_COUNT) {
      return readError("Kart bundle has too many entries",
                       QString("count=%1 max=%2").arg(entryCount).arg(KartFormat::MAX_ENTRY_COUNT));
    }

    // The per-entry flags byte (KartFormat::EntryFlag) is read so the entry
    // header advances correctly and the value round-trips into ExtractedFile
    // for forward-compatibility, but it is intentionally NOT applied to the
    // extracted file's permissions (Kartend-gnl9q). A .kart bundle is untrusted
    // content; honoring a stored exec bit would let a crafted bundle mark
    // extracted payloads executable. Extracted files therefore receive
    // umask-default permissions by design.
    auto flagsRes = readU8(f);
    if (flagsRes.isError()) return flagsRes.error();
    auto compRes = readU8(f);
    if (compRes.isError()) return compRes.error();
    auto pathLenRes = readU16LE(f);
    if (pathLenRes.isError()) return pathLenRes.error();
    if (pathLenRes.value() == 0 || pathLenRes.value() > KartFormat::MAX_PATH_LEN) {
      return readError("Entry path length out of range", QString("len=%1").arg(pathLenRes.value()));
    }
    auto pathBytes = readExactly(f, pathLenRes.value());
    if (pathBytes.isError()) return pathBytes.error();
    const QString relPath = QString::fromUtf8(pathBytes.value());

    auto origSizeRes = readU64LE(f);
    if (origSizeRes.isError()) return origSizeRes.error();
    auto payloadSizeRes = readU64LE(f);
    if (payloadSizeRes.isError()) return payloadSizeRes.error();
    if (origSizeRes.value() > KartFormat::MAX_ENTRY_SIZE ||
        payloadSizeRes.value() > KartFormat::MAX_ENTRY_SIZE) {
      return readError("Entry size out of range", QString("orig=%1 payload=%2 max=%3")
                                                      .arg(origSizeRes.value())
                                                      .arg(payloadSizeRes.value())
                                                      .arg(KartFormat::MAX_ENTRY_SIZE));
    }
    auto sha = readExactly(f, KartFormat::SHA256_SIZE);
    if (sha.isError()) return sha.error();
    const quint64 origSize = origSizeRes.value();
    const quint64 payloadSize = payloadSizeRes.value();

    // The whole-payload readExactly used to enforce this bound; with the
    // payload now streamed in chunks, check the declared length against the
    // bytes actually left in the file up front, before any destination file
    // is created for it.
    const qint64 payloadRemaining = f.size() - f.pos();
    if (static_cast<qint64>(payloadSize) > payloadRemaining) {
      return readError("Declared length exceeds remaining file size",
                       QString("requested=%1 remaining=%2 at offset=%3")
                           .arg(payloadSize)
                           .arg(payloadRemaining)
                           .arg(f.pos()));
    }

    if (!isPathSafe(relPath)) {
      return readError("Unsafe entry path (traversal or absolute)", relPath);
    }

    const QString destPath = dest.absoluteFilePath(relPath);
    const QString cleaned = QDir::cleanPath(destPath);

    // Resolve symlinks along the entry path before deciding it's safe.
    // isPathSafe catches '..' / absolute relPaths textually, but a parent
    // directory that's a symlink pointing outside destAbs slips past
    // startsWith() — cleaned still looks like a child of destAbs. Walk up
    // until we hit an existing ancestor, canonicalize it (collapses any
    // symlink in the chain), then re-attach the not-yet-created tail.
    // QDir::relativeFilePath on canonical paths rejects the escape that
    // startsWith() can't see.
    const QString destCanonical = QFileInfo(destAbs).canonicalFilePath();
    if (destCanonical.isEmpty()) {
      return readError("Cannot canonicalize destination directory", destAbs);
    }

    QString entryCanonical;
    {
      QString probe = cleaned;
      QString tail;
      while (true) {
        const QFileInfo probeFi(probe);
        if (probeFi.exists()) {
          const QString resolved = probeFi.canonicalFilePath();
          if (resolved.isEmpty()) {
            return readError("Cannot canonicalize ancestor of entry path", probe);
          }
          entryCanonical = tail.isEmpty() ? resolved : QDir(resolved).absoluteFilePath(tail);
          break;
        }
        const int sep = probe.lastIndexOf('/');
        if (sep <= 0) {
          // Walked off the front without finding an existing ancestor —
          // nothing in the chain to resolve a symlink through; the
          // already-cleaned absolute path is the best we have.
          entryCanonical = cleaned;
          break;
        }
        const QString seg = probe.mid(sep + 1);
        tail = tail.isEmpty() ? seg : seg + '/' + tail;
        probe = probe.left(sep);
      }
    }

    const QString rel = QDir(destCanonical).relativeFilePath(entryCanonical);
    if (rel == QLatin1String("..") || rel.startsWith(QLatin1String("../"))) {
      return readError("Entry path escapes destination directory (canonical)", relPath);
    }

    const auto algo = static_cast<KartFormat::Compression>(compRes.value());
    if (algo != KartFormat::Compression_None && algo != KartFormat::Compression_Zstd &&
        algo != KartFormat::Compression_Zlib) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "Unknown compression algorithm",
                                             "KartReader::extractTo")
          .withDetails(QString("algo=%1").arg(compRes.value()));
    }
    // Stored entries always record payload_size == original_size (the writer
    // sets both from the raw byte count); a disagreement can only come from a
    // tampered header.
    if (algo == KartFormat::Compression_None && payloadSize != origSize) {
      return readError("Stored entry sizes disagree",
                       QString("orig=%1 payload=%2").arg(origSize).arg(payloadSize));
    }

    QFileInfo fi(cleaned);
    if (!QDir().mkpath(fi.absolutePath())) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                             "Cannot create entry parent directory",
                                             "KartReader::extractTo")
          .withDetails(fi.absolutePath());
    }
    QSaveFile out(cleaned);
    if (!out.open(QIODevice::WriteOnly)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                             "Cannot open entry output file",
                                             "KartReader::extractTo")
          .withDetails(out.errorString());
    }

    // The payload streams through decompression into the QSaveFile in
    // STREAM_CHUNK_SIZE slices — peak memory is O(chunk) instead of
    // payload + original (up to 2 x MAX_ENTRY_SIZE for a single entry). The
    // SHA-256 accumulates incrementally over the decompressed bytes, and the
    // declared original_size plus the whole-archive MAX_TOTAL_EXTRACTED_BYTES
    // backstop (Kartend-4e8ku) are enforced per block, so a decompression
    // bomb aborts before its excess output is ever materialized. A failed
    // entry never lands: QSaveFile discards the temp file unless commit()
    // runs.
    QCryptographicHash entryHash(QCryptographicHash::Sha256);
    quint64 entryWritten = 0;

    auto writeBlock = [&](const QByteArray &block) -> ErrorUtils::Result<void> {
      if (out.write(block) != block.size()) {
        return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                               "Short write to entry output file",
                                               "KartReader::extractTo")
            .withDetails(cleaned);
      }
      entryHash.addData(block);
      entryWritten += static_cast<quint64>(block.size());
      totalExtractedBytes += static_cast<quint64>(block.size());
      if (totalExtractedBytes > KartFormat::MAX_TOTAL_EXTRACTED_BYTES) {
        return readError("Kart bundle exceeds the total extraction size limit",
                         QString("total=%1 max=%2")
                             .arg(totalExtractedBytes)
                             .arg(KartFormat::MAX_TOTAL_EXTRACTED_BYTES));
      }
      return {};
    };

    if (algo == KartFormat::Compression_None) {
      quint64 left = payloadSize;
      while (left > 0) {
        if (m_cancel.loadRelaxed()) {
          return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::OperationCancelled,
                                                 "Kart extraction cancelled",
                                                 "KartReader::extractTo");
        }
        const qint64 step =
            static_cast<qint64>(qMin<quint64>(left, KartCompression::STREAM_CHUNK_SIZE));
        auto chunk = readExactly(f, step);
        if (chunk.isError()) return chunk.error();
        if (auto w = writeBlock(chunk.value()); w.isError()) return w.error();
        left -= static_cast<quint64>(step);
      }
    } else if (algo == KartFormat::Compression_Zstd) {
      KartCompression::StreamDecompressor dec;
      if (auto beginRes = dec.begin(); beginRes.isError()) return beginRes.error();

      auto pumpBlock = [&](const QByteArray &block) -> ErrorUtils::Result<void> {
        // Enforce the declared size before writing the block — the zip-bomb
        // guard has to fire as soon as the decompressed stream exceeds it.
        if (entryWritten + static_cast<quint64>(block.size()) > origSize) {
          return ErrorUtils::ErrorContext::error(
                     ErrorUtils::ErrorCode::KartCompressionFailed,
                     "Decompressed entry exceeds declared original size",
                     "KartReader::extractTo")
              .withDetails(QString("declared=%1 path=%2").arg(origSize).arg(relPath));
        }
        if (block.isEmpty()) return {};
        return writeBlock(block);
      };

      quint64 left = payloadSize;
      while (left > 0) {
        if (m_cancel.loadRelaxed()) {
          return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::OperationCancelled,
                                                 "Kart extraction cancelled",
                                                 "KartReader::extractTo");
        }
        const qint64 step =
            static_cast<qint64>(qMin<quint64>(left, KartCompression::STREAM_CHUNK_SIZE));
        auto chunk = readExactly(f, step);
        if (chunk.isError()) return chunk.error();
        left -= static_cast<quint64>(step);

        qsizetype inPos = 0;
        while (inPos < chunk.value().size()) {
          auto block = dec.decompressChunk(chunk.value(), inPos);
          if (block.isError()) return block.error();
          if (auto w = pumpBlock(block.value()); w.isError()) return w.error();
        }
      }
      // Drain output zstd may still buffer after the last input byte; a frame
      // that stalls without closing means the payload was truncated.
      while (!dec.atFrameEnd()) {
        qsizetype inPos = 0;
        auto block = dec.decompressChunk(QByteArray(), inPos);
        if (block.isError()) return block.error();
        if (block.value().isEmpty() && !dec.atFrameEnd()) {
          return readError("Truncated zstd stream in Kart entry", relPath);
        }
        if (auto w = pumpBlock(block.value()); w.isError()) return w.error();
      }
      if (entryWritten != origSize) {
        return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                               "zstd decompressed size mismatch",
                                               "KartReader::extractTo")
            .withDetails(QString("expected=%1 got=%2").arg(origSize).arg(entryWritten));
      }
    } else {
      // Compression_Zlib stays buffered: qUncompress has no streaming form
      // without a direct zlib dependency, and zlib entries are only written
      // by no-zstd builds. Their decompressed size is additionally bounded by
      // qCompress's 4-byte length prefix on top of the MAX_ENTRY_SIZE checks
      // above.
      auto payload = readExactly(f, static_cast<qint64>(payloadSize));
      if (payload.isError()) return payload.error();
      auto raw = KartCompression::decompress(payload.value(), algo, static_cast<qint64>(origSize));
      if (raw.isError()) return raw.error();
      if (auto w = writeBlock(raw.value()); w.isError()) return w.error();
    }

    if (entryHash.result() != sha.value()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartIntegrityCheckFailed,
                                             "SHA-256 mismatch on Kart entry",
                                             "KartReader::extractTo")
          .withDetails(relPath);
    }

    if (!out.commit()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                             "Failed to commit entry output file",
                                             "KartReader::extractTo")
          .withDetails(out.errorString());
    }
    dirsToSync.insert(fi.absolutePath());

    // ExtractedFile::flags carries the stored per-entry flags byte for
    // forward-compat only; it is deliberately not honored for file permissions
    // (see the flags read site above, Kartend-gnl9q).
    ExtractedFile ef;
    ef.relPath = relPath;
    ef.absPath = cleaned;
    ef.flags = flagsRes.value();
    result.files.append(ef);

    emit entryExtracted(relPath);
    if (totalSize > 0) {
      emit progress(static_cast<double>(f.pos()) / static_cast<double>(totalSize));
    }
  }

  for (const QString &dir : dirsToSync) {
    PathUtils::syncDirectory(dir);
  }

  return result;
}

} // namespace KartReader
