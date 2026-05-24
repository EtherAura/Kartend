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
