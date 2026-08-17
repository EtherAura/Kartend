#ifndef KARTFORMAT_H
#define KARTFORMAT_H

#include <QtGlobal>

#include <array>

namespace KartFormat {

// File layout (little-endian throughout):
//   [magic 8B] [manifest_size u32] [manifest JSON UTF-8]
//   then a sequence of entries until EOF, each:
//     [flags u8] [compression u8] [path_len u16]
//     [path UTF-8 path_len bytes]
//     [original_size u64] [payload_size u64]
//     [sha256 32B (of UNCOMPRESSED payload)]
//     [payload payload_size bytes]
//
// The trailing byte of MAGIC is the binary-format version. Bumping it
// signals a backwards-incompatible layout change; manifest schema changes
// instead bump the format_version field inside the manifest JSON.

inline constexpr std::array<char, 8> MAGIC = {'K', 'A', 'R', 'T', '\0', '\0', '\0', '\1'};
inline constexpr int MAGIC_SIZE = 8;

// v1 → v2 (Kartend-kmj1): added the optional `playlists` array carrying the
// collection's static + smart playlists plus their items. A v2 reader
// silently treats a missing `playlists` field as empty so v1 bundles
// continue to round-trip without playlists.
inline constexpr quint32 CURRENT_VERSION = 2;

inline constexpr int SHA256_SIZE = 32;

inline constexpr int MAX_PATH_LEN = 4096;

inline constexpr quint32 MAX_MANIFEST_SIZE = 64u * 1024u * 1024u;

inline constexpr quint64 MAX_ENTRY_SIZE = 8ull * 1024ull * 1024ull * 1024ull;

// Aggregate decompression-bomb guards for the whole-archive extraction loop,
// complementing the per-entry MAX_ENTRY_SIZE / MAX_PATH_LEN checks. A crafted
// bundle can pass every per-entry check yet still exhaust inodes (millions of
// tiny entries) or disk (many near-MAX_ENTRY_SIZE entries) in aggregate.
//
// MAX_ENTRY_COUNT mirrors archiverepack.cpp's kMaxEntries (200000) so the
// inode-exhaustion ceiling is recognizable across extractors.
//
// MAX_TOTAL_EXTRACTED_BYTES is a deliberately generous sanity backstop, NOT a
// tight quota: a legitimate .kart can carry an entire media collection, so this
// sits well above launchmanagerarchive's per-launch MAX_EXTRACTION_BYTES (4 GiB)
// — it exists only to stop a runaway/abusive bundle from filling the disk.
inline constexpr quint64 MAX_ENTRY_COUNT = 200000;

inline constexpr quint64 MAX_TOTAL_EXTRACTED_BYTES = 2048ull * 1024ull * 1024ull * 1024ull;

// Manifest-array ceilings, enforced by KartManifest::parse. MAX_MANIFEST_SIZE
// bounds the JSON text, but the parsed structures expand far beyond the text
// that describes them, so parse also rejects (never truncates — matching the
// extraction loop's fail-on-excess contract) any manifest whose arrays exceed
// these counts. MAX_MANIFEST_ITEMS reuses MAX_ENTRY_COUNT: a manifest cannot
// meaningfully describe more items than a bundle may carry entries, and the
// same ceiling applies to one playlist's item list. Launchers and playlists
// number at most dozens in any real collection; 10000 is a generous ceiling.
inline constexpr qsizetype MAX_MANIFEST_ITEMS = static_cast<qsizetype>(MAX_ENTRY_COUNT);

inline constexpr qsizetype MAX_MANIFEST_LAUNCHERS = 10000;

inline constexpr qsizetype MAX_MANIFEST_PLAYLISTS = 10000;

// One item links at most a handful of artwork types (seven standard ids plus
// the user's custom types); 256 is far above any real gallery.
inline constexpr qsizetype MAX_MANIFEST_ARTWORK_LINKS_PER_ITEM = 256;

enum EntryFlag : quint8 {
  Flag_Media = 0x01,
  Flag_Artwork = 0x02,
  Flag_Video = 0x04,
  Flag_Manual = 0x08,
};

enum Compression : quint8 {
  Compression_None = 0,
  Compression_Zstd = 1,
  Compression_Zlib = 2,
};

[[nodiscard]] inline bool hasMagic(const char *bytes, qint64 len) {
  if (!bytes || len < MAGIC_SIZE) {
    return false;
  }
  for (int i = 0; i < MAGIC_SIZE; ++i) {
    if (bytes[i] != MAGIC[i]) {
      return false;
    }
  }
  return true;
}

} // namespace KartFormat

#endif
