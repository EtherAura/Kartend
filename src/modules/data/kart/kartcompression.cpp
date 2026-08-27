#include "kartcompression.h"

#ifdef KARTEND_HAS_ZSTD
#include <zstd.h>
#endif

#include <limits>
#include <QtEndian>

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
    // Guard the narrowing to qsizetype: on 32-bit targets qsizetype is 32-bit,
    // and the validation above permits expectedSize up to MAX_ENTRY_SIZE (8 GiB).
    // Reject anything that won't fit the buffer's index type rather than
    // truncating it (the prior static_cast<int> could wrap to a tiny/negative
    // size while zstd was still told the full capacity — a heap overflow).
    if (expectedSize > static_cast<qint64>(std::numeric_limits<qsizetype>::max())) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "Decompressed size exceeds addressable range",
                                             "KartCompression::decompress");
    }
    QByteArray out;
    out.resize(static_cast<qsizetype>(expectedSize));
    // Hand zstd the actual buffer capacity (out.size()), never a separately
    // derived value, so the destination size can never exceed the allocation.
    const size_t result =
        ZSTD_decompress(out.data(), static_cast<size_t>(out.size()), compressed.constData(),
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
    // qUncompress sizes its output allocation from a 4-byte big-endian prefix
    // embedded in the COMPRESSED bytes. That prefix is attacker-controlled and
    // wholly independent of the entry header's origSize — which is the value
    // MAX_ENTRY_SIZE was checked against upstream — so a ~30-byte payload whose
    // prefix claims 4 GiB reached a 4 GiB allocation attempt before any cap of
    // ours could speak. The size comparison below cannot help: it is post-hoc
    // by construction, inspecting a buffer qUncompress has already allocated
    // (Kartend-ohqfs).
    //
    // Note this is NOT mitigated by zlib entries only being written by no-zstd
    // builds. That is a property of our WRITER; the reader accepts
    // Compression_Zlib from any bundle with no gate, so a hostile bundle simply
    // always selects zlib.
    //
    // So validate the prefix ourselves, before qUncompress sees the bytes —
    // mirroring the zstd branch above, which never lets a declared size reach
    // an allocator unchecked. Starting with that branch's own range gate: the
    // reader's only call site passes the header's origSize, which is unsigned
    // and already capped, so this rejects nothing legitimate.
    if (expectedSize < 0 || static_cast<quint64>(expectedSize) > KartFormat::MAX_ENTRY_SIZE) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "Decompressed size out of range",
                                             "KartCompression::decompress");
    }
    constexpr qsizetype kSizePrefixBytes = 4;
    if (compressed.size() < kSizePrefixBytes) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "zlib payload too short to carry a size prefix",
                                             "KartCompression::decompress")
          .withDetails(QString("payloadBytes=%1").arg(compressed.size()));
    }
    // The header already stated this entry's size and that value is bounded, so
    // a prefix disagreeing with it IS the attack — not a mismatch worth
    // discovering after qUncompress has sized a buffer from it.
    const auto prefixSize = static_cast<qint64>(
        qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(compressed.constData())));
    if (prefixSize != expectedSize) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "zlib size prefix disagrees with entry header",
                                             "KartCompression::decompress")
          .withDetails(QString("prefix=%1 header=%2").arg(prefixSize).arg(expectedSize));
    }
    const QByteArray result = qUncompress(compressed);
    if (result.isEmpty() && !compressed.isEmpty()) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "qUncompress (zlib) failed",
                                             "KartCompression::decompress");
    }
    if (result.size() != expectedSize) {
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
    out.resize(static_cast<qsizetype>(bound));
    const size_t result = ZSTD_compress(out.data(), static_cast<size_t>(out.size()),
                                        raw.constData(), static_cast<size_t>(raw.size()), 3);
    if (ZSTD_isError(result)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "zstd compression failed", "KartCompression::compress")
          .withDetails(QString::fromUtf8(ZSTD_getErrorName(result)));
    }
    out.resize(static_cast<qsizetype>(result));
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

StreamCompressor::~StreamCompressor() {
#ifdef KARTEND_HAS_ZSTD
  if (m_ctx) {
    ZSTD_freeCCtx(static_cast<ZSTD_CCtx *>(m_ctx));
  }
#endif
}

ErrorUtils::Result<void> StreamCompressor::begin(quint64 pledgedSize) {
#ifdef KARTEND_HAS_ZSTD
  if (m_ctx) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Stream compressor already started",
                                           "KartCompression::StreamCompressor::begin");
  }
  ZSTD_CCtx *ctx = ZSTD_createCCtx();
  if (!ctx) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Cannot allocate zstd compression context",
                                           "KartCompression::StreamCompressor::begin");
  }
  // Level 3 matches the one-shot ZSTD_compress call above; the pledged source
  // size puts the content size into the frame header the way the one-shot API
  // does, and makes ZSTD_e_end fail if the streamed byte count disagrees.
  size_t rc = ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, 3);
  if (!ZSTD_isError(rc)) {
    rc = ZSTD_CCtx_setPledgedSrcSize(ctx, pledgedSize);
  }
  if (ZSTD_isError(rc)) {
    ZSTD_freeCCtx(ctx);
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Cannot configure zstd compression context",
                                           "KartCompression::StreamCompressor::begin")
        .withDetails(QString::fromUtf8(ZSTD_getErrorName(rc)));
  }
  m_ctx = ctx;
  return {};
#else
  Q_UNUSED(pledgedSize);
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                         "zstd compression requested but build lacks zstd",
                                         "KartCompression::StreamCompressor::begin");
#endif
}

ErrorUtils::Result<QByteArray> StreamCompressor::compress(const QByteArray &chunk) {
#ifdef KARTEND_HAS_ZSTD
  if (!m_ctx) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Stream compressor not started",
                                           "KartCompression::StreamCompressor::compress");
  }
  auto *ctx = static_cast<ZSTD_CCtx *>(m_ctx);
  QByteArray out;
  ZSTD_inBuffer in{chunk.constData(), static_cast<size_t>(chunk.size()), 0};
  while (in.pos < in.size) {
    QByteArray block(static_cast<qsizetype>(STREAM_CHUNK_SIZE), Qt::Uninitialized);
    ZSTD_outBuffer ob{block.data(), static_cast<size_t>(block.size()), 0};
    const size_t rc = ZSTD_compressStream2(ctx, &ob, &in, ZSTD_e_continue);
    if (ZSTD_isError(rc)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "zstd streaming compression failed",
                                             "KartCompression::StreamCompressor::compress")
          .withDetails(QString::fromUtf8(ZSTD_getErrorName(rc)));
    }
    block.resize(static_cast<qsizetype>(ob.pos));
    out.append(block);
  }
  return out;
#else
  Q_UNUSED(chunk);
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                         "zstd compression requested but build lacks zstd",
                                         "KartCompression::StreamCompressor::compress");
#endif
}

ErrorUtils::Result<QByteArray> StreamCompressor::finish() {
#ifdef KARTEND_HAS_ZSTD
  if (!m_ctx) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Stream compressor not started",
                                           "KartCompression::StreamCompressor::finish");
  }
  auto *ctx = static_cast<ZSTD_CCtx *>(m_ctx);
  QByteArray out;
  ZSTD_inBuffer in{nullptr, 0, 0};
  while (true) {
    QByteArray block(static_cast<qsizetype>(STREAM_CHUNK_SIZE), Qt::Uninitialized);
    ZSTD_outBuffer ob{block.data(), static_cast<size_t>(block.size()), 0};
    // A pledged-size mismatch (source file changed mid-stream) surfaces here.
    const size_t remaining = ZSTD_compressStream2(ctx, &ob, &in, ZSTD_e_end);
    if (ZSTD_isError(remaining)) {
      return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                             "zstd streaming compression failed",
                                             "KartCompression::StreamCompressor::finish")
          .withDetails(QString::fromUtf8(ZSTD_getErrorName(remaining)));
    }
    block.resize(static_cast<qsizetype>(ob.pos));
    out.append(block);
    if (remaining == 0) {
      break;
    }
  }
  return out;
#else
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                         "zstd compression requested but build lacks zstd",
                                         "KartCompression::StreamCompressor::finish");
#endif
}

StreamDecompressor::~StreamDecompressor() {
#ifdef KARTEND_HAS_ZSTD
  if (m_ctx) {
    ZSTD_freeDCtx(static_cast<ZSTD_DCtx *>(m_ctx));
  }
#endif
}

ErrorUtils::Result<void> StreamDecompressor::begin() {
#ifdef KARTEND_HAS_ZSTD
  if (m_ctx) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Stream decompressor already started",
                                           "KartCompression::StreamDecompressor::begin");
  }
  ZSTD_DCtx *ctx = ZSTD_createDCtx();
  if (!ctx) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Cannot allocate zstd decompression context",
                                           "KartCompression::StreamDecompressor::begin");
  }
  m_ctx = ctx;
  return {};
#else
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                         "Kart entry uses zstd but this build lacks zstd support",
                                         "KartCompression::StreamDecompressor::begin");
#endif
}

ErrorUtils::Result<QByteArray> StreamDecompressor::decompressChunk(const QByteArray &input,
                                                                   qsizetype &inPos) {
#ifdef KARTEND_HAS_ZSTD
  if (!m_ctx) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Stream decompressor not started",
                                           "KartCompression::StreamDecompressor::decompressChunk");
  }
  if (inPos < 0 || inPos > input.size()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "Stream decompressor input position out of range",
                                           "KartCompression::StreamDecompressor::decompressChunk")
        .withDetails(QString("pos=%1 size=%2").arg(inPos).arg(input.size()));
  }
  auto *ctx = static_cast<ZSTD_DCtx *>(m_ctx);
  QByteArray block(static_cast<qsizetype>(STREAM_CHUNK_SIZE), Qt::Uninitialized);
  ZSTD_inBuffer in{input.constData(), static_cast<size_t>(input.size()),
                   static_cast<size_t>(inPos)};
  ZSTD_outBuffer ob{block.data(), static_cast<size_t>(block.size()), 0};
  const size_t rc = ZSTD_decompressStream(ctx, &ob, &in);
  if (ZSTD_isError(rc)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                           "zstd decompression failed",
                                           "KartCompression::StreamDecompressor::decompressChunk")
        .withDetails(QString::fromUtf8(ZSTD_getErrorName(rc)));
  }
  inPos = static_cast<qsizetype>(in.pos);
  // rc == 0 marks a completed frame; like the one-shot ZSTD_decompress, a
  // subsequent call with more input simply starts decoding the next frame.
  m_frameEnded = (rc == 0);
  block.resize(static_cast<qsizetype>(ob.pos));
  return block;
#else
  Q_UNUSED(input);
  Q_UNUSED(inPos);
  return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::KartCompressionFailed,
                                         "Kart entry uses zstd but this build lacks zstd support",
                                         "KartCompression::StreamDecompressor::decompressChunk");
#endif
}

} // namespace KartCompression
