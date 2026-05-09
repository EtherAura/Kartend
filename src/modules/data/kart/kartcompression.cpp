#include "kartcompression.h"

#ifdef KARTEND_HAS_ZSTD
#include <zstd.h>
#endif

namespace KartCompression {

bool zstdAvailable() {
#ifdef KARTEND_HAS_ZSTD
  return true;
#else
  return false;
#endif
}

ErrorUtils::Result<QByteArray> decompress(const QByteArray &compressed,
                                          KartFormat::Compression algo, qint64 expectedSize) {
  switch (algo) {
  case KartFormat::Compression_None:
    return compressed;

  case KartFormat::Compression_Zstd: {
#ifdef KARTEND_HAS_ZSTD
    if (expectedSize < 0 || static_cast<quint64>(expectedSize) > KartFormat::MAX_ENTRY_SIZE) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "Decompressed size out of range",
                                             "KartCompression::decompress");
    }
    QByteArray out;
    out.resize(static_cast<int>(expectedSize));
    const size_t result =
        ZSTD_decompress(out.data(), static_cast<size_t>(expectedSize), compressed.constData(),
                        static_cast<size_t>(compressed.size()));
    if (ZSTD_isError(result)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "zstd decompression failed",
                                             "KartCompression::decompress")
          .withDetails(QString::fromUtf8(ZSTD_getErrorName(result)));
    }
    if (static_cast<qint64>(result) != expectedSize) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "zstd decompressed size mismatch",
                                             "KartCompression::decompress")
          .withDetails(
              QString("expected=%1 got=%2").arg(expectedSize).arg(static_cast<qint64>(result)));
    }
    return out;
#else
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Kart entry uses zstd but this build lacks zstd support",
                                           "KartCompression::decompress");
#endif
  }

  case KartFormat::Compression_Zlib: {
    const QByteArray result = qUncompress(compressed);
    if (result.isEmpty() && !compressed.isEmpty()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "qUncompress (zlib) failed",
                                             "KartCompression::decompress");
    }
    if (expectedSize >= 0 && result.size() != expectedSize) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "zlib decompressed size mismatch",
                                             "KartCompression::decompress")
          .withDetails(QString("expected=%1 got=%2").arg(expectedSize).arg(result.size()));
    }
    return result;
  }
  }

  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                         "Unknown compression algorithm",
                                         "KartCompression::decompress")
      .withDetails(QString("algo=%1").arg(static_cast<int>(algo)));
}

ErrorUtils::Result<QByteArray> compress(const QByteArray &raw, KartFormat::Compression algo) {
  switch (algo) {
  case KartFormat::Compression_None:
    return raw;

  case KartFormat::Compression_Zstd: {
#ifdef KARTEND_HAS_ZSTD
    const size_t bound = ZSTD_compressBound(static_cast<size_t>(raw.size()));
    QByteArray out;
    out.resize(static_cast<int>(bound));
    const size_t result =
        ZSTD_compress(out.data(), bound, raw.constData(), static_cast<size_t>(raw.size()), 3);
    if (ZSTD_isError(result)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "zstd compression failed", "KartCompression::compress")
          .withDetails(QString::fromUtf8(ZSTD_getErrorName(result)));
    }
    out.resize(static_cast<int>(result));
    return out;
#else
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "zstd compression requested but build lacks zstd",
                                           "KartCompression::compress");
#endif
  }

  case KartFormat::Compression_Zlib:
    return qCompress(raw, 6);
  }

  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                         "Unknown compression algorithm",
                                         "KartCompression::compress")
      .withDetails(QString("algo=%1").arg(static_cast<int>(algo)));
}

} // namespace KartCompression
