#include "kartreader.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "kartcompression.h"
#include "kartformat.h"

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
  QByteArray buf;
  buf.resize(static_cast<int>(n));
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

bool isPathSafe(const QString &relPath) {
  if (relPath.isEmpty()) return false;
  if (relPath.contains(QChar('\0'))) return false;
  if (relPath.startsWith('/') || relPath.startsWith('\\')) return false;
  if (relPath.size() >= 2 && relPath.at(1) == QChar(':')) return false;
  const QStringList segments = relPath.split('/', Qt::SkipEmptyParts);
  for (const QString &seg : segments) {
    if (seg == "..") return false;
    if (seg.contains('\\')) return false;
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

  while (!f.atEnd()) {
    if (m_cancel.loadRelaxed()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::OperationCancelled,
                                             "Kart extraction cancelled", "KartReader::extractTo");
    }

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
    auto payload = readExactly(f, static_cast<qint64>(payloadSizeRes.value()));
    if (payload.isError()) return payload.error();

    if (!isPathSafe(relPath)) {
      return readError("Unsafe entry path (traversal or absolute)", relPath);
    }

    const QString destPath = dest.absoluteFilePath(relPath);
    const QString cleaned = QDir::cleanPath(destPath);
    if (!cleaned.startsWith(destAbs + '/') && cleaned != destAbs) {
      return readError("Entry path escapes destination directory", relPath);
    }

    const auto algo = static_cast<KartFormat::Compression>(compRes.value());
    auto raw = KartCompression::decompress(payload.value(), algo,
                                           static_cast<qint64>(origSizeRes.value()));
    if (raw.isError()) return raw.error();

    QByteArray actualSha = QCryptographicHash::hash(raw.value(), QCryptographicHash::Sha256);
    if (actualSha != sha.value()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartIntegrityCheckFailed,
                                             "SHA-256 mismatch on Kart entry",
                                             "KartReader::extractTo")
          .withDetails(relPath);
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
    if (out.write(raw.value()) != raw.value().size()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                             "Short write to entry output file",
                                             "KartReader::extractTo")
          .withDetails(cleaned);
    }
    if (!out.commit()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::FileWriteError,
                                             "Failed to commit entry output file",
                                             "KartReader::extractTo")
          .withDetails(out.errorString());
    }

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

  return result;
}

} // namespace KartReader
