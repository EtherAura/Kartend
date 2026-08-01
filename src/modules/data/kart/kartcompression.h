#ifndef KARTCOMPRESSION_H
#define KARTCOMPRESSION_H

#include <QByteArray>

#include "errorutils.h"
#include "kartformat.h"

namespace KartCompression {

// 1 MiB streaming chunk — the same sweet spot RomHasher uses: negligible
// memory pressure, and the syscall overhead is amortized by the kernel's
// readahead cache. Writer and reader stream entry payloads in slices of this
// size so peak memory stays O(chunk) regardless of entry size (entries may
// legitimately reach KartFormat::MAX_ENTRY_SIZE, 8 GiB).
inline constexpr qint64 STREAM_CHUNK_SIZE = 1 << 20;

[[nodiscard]] bool zstdAvailable();

// One-shot codecs, kept for small buffers (zlib entries and the size-bounded
// manifest paths). Large zstd/stored entry payloads go through the streaming
// classes below instead.
[[nodiscard]] ErrorUtils::Result<QByteArray>
decompress(const QByteArray &compressed, KartFormat::Compression algo, qint64 expectedSize);

[[nodiscard]] ErrorUtils::Result<QByteArray> compress(const QByteArray &raw,
                                                      KartFormat::Compression algo);

// Incremental zstd compressor for large entry payloads. The one-shot
// compress() must hold the whole input and output simultaneously; the writer
// instead feeds STREAM_CHUNK_SIZE slices through this context so peak memory
// stays O(chunk). begin() fails on builds without zstd — callers pick the
// buffered zlib path before ever reaching it.
class StreamCompressor {
public:
  StreamCompressor() = default;
  ~StreamCompressor();
  StreamCompressor(const StreamCompressor &) = delete;
  StreamCompressor &operator=(const StreamCompressor &) = delete;

  // pledgedSize is the exact total input size; zstd embeds it in the frame
  // header (matching the one-shot API's frames) and finish() fails if the fed
  // bytes disagree — a free tripwire for a source file mutating mid-export.
  [[nodiscard]] ErrorUtils::Result<void> begin(quint64 pledgedSize);
  // Compresses one input slice, returning whatever output zstd produced for
  // it (possibly empty while the context buffers internally).
  [[nodiscard]] ErrorUtils::Result<QByteArray> compress(const QByteArray &chunk);
  // Flushes the epilogue and closes the frame.
  [[nodiscard]] ErrorUtils::Result<QByteArray> finish();

private:
  void *m_ctx = nullptr; // ZSTD_CCtx — void* keeps zstd.h out of this header
};

// Incremental zstd decompressor. decompressChunk consumes compressed bytes
// from `input` starting at `inPos` (advancing it) and returns at most
// STREAM_CHUNK_SIZE decompressed bytes per call, so the caller can enforce
// the header-declared original-size bound between pulls — the zip-bomb guard
// has to fire before excess output is materialized, not after. Call with the
// input fully consumed (or an empty input) to drain buffered output until
// atFrameEnd() reports the frame closed.
class StreamDecompressor {
public:
  StreamDecompressor() = default;
  ~StreamDecompressor();
  StreamDecompressor(const StreamDecompressor &) = delete;
  StreamDecompressor &operator=(const StreamDecompressor &) = delete;

  [[nodiscard]] ErrorUtils::Result<void> begin();
  [[nodiscard]] ErrorUtils::Result<QByteArray> decompressChunk(const QByteArray &input,
                                                               qsizetype &inPos);
  [[nodiscard]] bool atFrameEnd() const { return m_frameEnded; }

private:
  void *m_ctx = nullptr; // ZSTD_DCtx — void* keeps zstd.h out of this header
  bool m_frameEnded = false;
};

} // namespace KartCompression

#endif
