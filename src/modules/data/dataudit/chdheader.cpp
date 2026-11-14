#include "chdheader.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QtEndian>

#include <array>

namespace DatAudit {

namespace {

constexpr qint64 kPrefixLength = 16;
constexpr char kChdTag[] = "MComprHD";
constexpr int kSha1Length = 20;

// Read up to `n` bytes into `out`, looping over short reads. Returns the
// byte count read, or -1 on a device error.
qint64 readUpTo(QIODevice &device, char *out, qint64 n) {
  qint64 got = 0;
  while (got < n) {
    const qint64 r = device.read(out + got, n - got);
    if (r < 0) return -1;
    if (r == 0) break;
    got += r;
  }
  return got;
}

// The fixed header length a supported version must declare; 0 for any
// version this reader does not recognise.
quint32 expectedHeaderLength(quint32 version) {
  switch (version) {
  case 3:
    return kChdV3HeaderLength;
  case 4:
    return kChdV4HeaderLength;
  case 5:
    return kChdV5HeaderLength;
  default:
    return 0;
  }
}

quint32 be32(const char *p) {
  return qFromBigEndian<quint32>(p);
}
quint64 be64(const char *p) {
  return qFromBigEndian<quint64>(p);
}

QString sha1Hex(const char *p) {
  return QString::fromLatin1(QByteArray(p, kSha1Length).toHex());
}

bool allZero(const char *p, int n) {
  for (int i = 0; i < n; ++i)
    if (p[i] != 0) return false;
  return true;
}

} // namespace

ChdHeader readChdHeader(QIODevice &device) {
  ChdHeader h;
  if (!device.isOpen() || !device.isReadable()) return h; // Unreadable

  std::array<char, kChdV5HeaderLength> buf{};
  const qint64 prefix = readUpTo(device, buf.data(), kPrefixLength);
  if (prefix < 0) return h; // Unreadable
  h.status = ChdHeaderStatus::Malformed;
  if (prefix < kPrefixLength || QByteArray(buf.data(), 8) != QByteArray(kChdTag)) return h;

  h.headerLength = be32(buf.data() + 8);
  h.version = be32(buf.data() + 12);
  const quint32 expected = expectedHeaderLength(h.version);
  // Unsupported version or a length that does not match the version's fixed
  // header: stop at the prefix rather than trusting the declared length.
  if (expected == 0 || h.headerLength != expected) return h;

  const qint64 rest = qint64(expected) - kPrefixLength;
  const qint64 got = readUpTo(device, buf.data() + kPrefixLength, rest);
  if (got < 0) {
    h.status = ChdHeaderStatus::Unreadable;
    return h;
  }
  if (got < rest) return h; // truncated header: Malformed

  if (h.version != 5) {
    h.status = ChdHeaderStatus::NeedsConversion;
    return h;
  }

  const char *p = buf.data();
  h.logicalBytes = be64(p + 32);
  h.hunkBytes = be32(p + 56);
  h.unitBytes = be32(p + 60);
  h.rawSha1 = sha1Hex(p + 64);
  h.combinedSha1 = sha1Hex(p + 84);
  if (!allZero(p + 104, kSha1Length)) h.parentSha1 = sha1Hex(p + 104);
  h.status = ChdHeaderStatus::Ok;
  return h;
}

ChdHeader readChdHeader(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return readChdHeader(file);
}

} // namespace DatAudit
