#ifndef CHDHEADER_H
#define CHDHEADER_H

#include <QString>
#include <QtGlobal>

class QIODevice;

/// Fixed-header reader for MAME CHD disk images (Kartend-xu36n).
///
/// A MAME listxml <disk sha1> is the CHD v5 combined raw+metadata SHA-1,
/// which the 124-byte header already carries, so a CHD can be identified
/// without reading (let alone hashing) its multi-GB payload. The reader only
/// ever looks at the fixed header: at most 124 bytes, never the map, the
/// metadata or the hunks, and it never allocates from a declared length.
///
/// Header layout (big-endian), v5: 0..8 tag "MComprHD", 8..12 header length
/// (124), 12..16 version, 32..40 logical bytes, 56..60 hunk bytes, 60..64
/// unit bytes, 64..84 raw SHA-1, 84..104 combined SHA-1, 104..124 parent
/// SHA-1 (all zero without a parent). Legacy v3 headers are 120 bytes and v4
/// headers 108 bytes; both need chdman conversion before MAME (0.146+) can
/// use them, so they are recognised but never yield an identity.
namespace DatAudit {

enum class ChdHeaderStatus {
  Ok,              ///< Complete v5 header; combinedSha1 is the identity.
  NeedsConversion, ///< Complete, correctly sized v3/v4 header.
  Malformed,       ///< Wrong tag, unsupported version, bad length, truncated.
  Unreadable,      ///< File cannot be opened, or the device read failed.
};

/// Fixed CHD header lengths per supported version (bytes).
inline constexpr quint32 kChdV3HeaderLength = 120;
inline constexpr quint32 kChdV4HeaderLength = 108;
inline constexpr quint32 kChdV5HeaderLength = 124;

struct ChdHeader {
  ChdHeaderStatus status = ChdHeaderStatus::Unreadable;
  /// Version and declared header length from the 16-byte prefix; 0 when the
  /// prefix itself could not be read.
  quint32 version = 0;
  quint32 headerLength = 0;
  /// v5 only (status Ok). Lowercase hex, the AuditRow convention. Empty for
  /// every other status, so a non-Ok header can never match by identity.
  QString combinedSha1;
  QString rawSha1;
  QString parentSha1; ///< Empty when the parent SHA-1 is all zero.
  quint64 logicalBytes = 0;
  quint32 hunkBytes = 0;
  quint32 unitBytes = 0;

  [[nodiscard]] bool hasParent() const { return !parentSha1.isEmpty(); }
};

/// Read the fixed header from the device's current position. The device
/// must be open for reading; at most 124 bytes are consumed.
[[nodiscard]] ChdHeader readChdHeader(QIODevice &device);

/// Open `path` read-only and read its fixed header.
[[nodiscard]] ChdHeader readChdHeader(const QString &path);

} // namespace DatAudit

#endif // CHDHEADER_H
