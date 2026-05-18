// Compile-time-bundled API credentials.
//
// ──────────────────────────────────────────────────────────────────
// To populate the bundled SS credentials after the SS dev-account
// application is approved:
//
//   1. Receive `dev_id` (plain string) and `dev_password` (plain
//      string) from the SS team.
//
//   2. Run the obfuscator below in your head (it's trivial — XOR
//      each byte with OBFUSCATION_KEY) or via a one-liner (single
//      command, joined here so the // comment doesn't trip
//      `-Wcomment` with a trailing line-continuation):
//
//        python3 -c 'k=0x5A; s="YOUR_DEV_PASSWORD_HERE"; print(",".join(str(ord(c)^k) for c in s))'
//
//   3. Paste the comma-separated decimal output into the corresponding
//      OBFUSCATED_* constant below. The dev_id is short and not
//      sensitive — paste it as-is into BUNDLED_DEV_ID below; the
//      dev_password (which IS sensitive) goes through the obfuscator.
//
//   4. Rebuild. The deobfuscator runs on every credential lookup,
//      which is once per scrape — overhead is negligible.
// ──────────────────────────────────────────────────────────────────
#include "bundledcredentials.h"

#include <QString>
#include <QStringList>

namespace BundledCredentials {

namespace {

// XOR key applied to every byte during obfuscation. Arbitrary —
// changing it just requires re-encoding the bundled values.
constexpr unsigned char OBFUSCATION_KEY = 0x5A;

// Bundled SS dev credentials issued by the screenscraper.fr team
// for Kartend (dev account "cedar"). dev_id is short, public-by-
// nature (it ends up in every API URL in plain text), and not
// sensitive — stored as a plain string.
constexpr const char *BUNDLED_DEV_ID = "cedar";

// dev_password XOR-obfuscated against OBFUSCATION_KEY. Comma-
// separated decimal bytes; deobfuscated on every credential lookup
// by `deobfuscate` below. Encoded by the python one-liner in this
// file's header comment.
constexpr const char *BUNDLED_DEV_PASSWORD_OBFUSCATED = "62,30,11,13,45,62,105,47,104,56,107";

QString deobfuscate(const QString &commaSeparatedDecimals) {
  if (commaSeparatedDecimals.isEmpty()) return {};
  QString out;
  out.reserve(commaSeparatedDecimals.size() / 4);
  for (const QString &part : commaSeparatedDecimals.split(',', Qt::SkipEmptyParts)) {
    bool ok = false;
    const int v = part.trimmed().toInt(&ok);
    if (!ok || v < 0 || v > 255) {
      // Malformed obfuscated string — return empty rather than emit
      // a garbled credential that would just produce a confusing
      // auth-failure error from the API.
      return {};
    }
    out.append(QChar(static_cast<unsigned char>(v) ^ OBFUSCATION_KEY));
  }
  return out;
}

} // namespace

ScreenScraperPair screenscraper() {
  ScreenScraperPair p;
  p.devId = QString::fromLatin1(BUNDLED_DEV_ID);
  p.devPassword = deobfuscate(QString::fromLatin1(BUNDLED_DEV_PASSWORD_OBFUSCATED));
  return p;
}

} // namespace BundledCredentials
