// Pure dedup / CRC-short-circuit helpers extracted from ScrapeResultDialog
// (Kartend-dpehr). See scrapeassetdedup.h for the rationale. No UI, no dialog
// state — unit-tested by tests/modules/scraper/test_scrapeassetdedup.cpp.
#include "scrapeassetdedup.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1String>
#include <QUrlQuery>

namespace ScrapeAssetDedup {

QStringList sharedAssetProbePaths(const Scraper::MediaAsset &asset, const QString &artworkDir) {
  if (asset.scope == Scraper::MediaScope::Game || asset.scopeKey.isEmpty() ||
      artworkDir.isEmpty()) {
    return {};
  }
  const QString scopePrefix = asset.scope == Scraper::MediaScope::Group
                                  ? QStringLiteral("group_")
                                  : QStringLiteral("company_");
  const QString dir = QDir(artworkDir).filePath(QStringLiteral("_shared/") + asset.type);
  // Probe png first (default), then common fallbacks. extensionForAsset in
  // scrapepersistence defaults to png for images; if a previous scrape used
  // outputformat=jpg or SS served webp, we'd still find it here.
  QStringList out;
  for (const char *ext : {"png", "jpg", "jpeg", "webp"}) {
    out.append(
        QDir(dir).filePath(scopePrefix + asset.scopeKey + QLatin1Char('.') + QLatin1String(ext)));
  }
  return out;
}

QString findExistingSharedAsset(const Scraper::MediaAsset &asset, const QStringList &searchPaths) {
  for (const QString &artDir : searchPaths) {
    for (const QString &candidate : sharedAssetProbePaths(asset, artDir)) {
      if (QFileInfo::exists(candidate)) {
        return candidate;
      }
    }
  }
  return QString();
}

QString findExistingPerGameAsset(const Scraper::MediaAsset &asset, const QString &artworkDir,
                                 const QString &baseName) {
  if (asset.scope != Scraper::MediaScope::Game || artworkDir.isEmpty() || baseName.isEmpty() ||
      asset.type.isEmpty()) {
    return {};
  }
  // Skip videos / manuals / non-image kinds — the CRC short-circuit is
  // documented for SS's mediaJeu.php image endpoints. Videos
  // (`mediaVideoJeu.php`) and manuals (`mediaManuelJeu.php`) don't accept the
  // hash params per SS docs.
  static const QStringList kSkipTypes = {QStringLiteral("video"), QStringLiteral("manual")};
  if (kSkipTypes.contains(asset.type.toLower())) return {};
  const QString dir = QDir(artworkDir).filePath(asset.type);
  for (const char *ext : {"png", "jpg", "jpeg", "webp"}) {
    const QString candidate = QDir(dir).filePath(baseName + QLatin1Char('.') + QLatin1String(ext));
    if (QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

LocalHashes hashLocalFile(const QString &path) {
  LocalHashes out;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return out;
  QCryptographicHash md5(QCryptographicHash::Md5);
  QCryptographicHash sha1(QCryptographicHash::Sha1);
  while (!f.atEnd()) {
    const QByteArray chunk = f.read(64 * 1024);
    md5.addData(chunk);
    sha1.addData(chunk);
  }
  out.md5Hex = QString::fromLatin1(md5.result().toHex());
  out.sha1Hex = QString::fromLatin1(sha1.result().toHex());
  return out;
}

QUrl withHashHints(const QUrl &original, const LocalHashes &hashes) {
  if (hashes.md5Hex.isEmpty() && hashes.sha1Hex.isEmpty() && hashes.crc32Hex.isEmpty()) {
    return original;
  }
  QUrl out(original);
  QUrlQuery q(out);
  if (!hashes.md5Hex.isEmpty() && !q.hasQueryItem(QStringLiteral("md5"))) {
    q.addQueryItem(QStringLiteral("md5"), hashes.md5Hex);
  }
  if (!hashes.sha1Hex.isEmpty() && !q.hasQueryItem(QStringLiteral("sha1"))) {
    q.addQueryItem(QStringLiteral("sha1"), hashes.sha1Hex);
  }
  if (!hashes.crc32Hex.isEmpty() && !q.hasQueryItem(QStringLiteral("crc"))) {
    q.addQueryItem(QStringLiteral("crc"), hashes.crc32Hex);
  }
  out.setQuery(q);
  return out;
}

bool isHashShortCircuit(const QByteArray &body) {
  if (body.size() > 32) return false;
  const QByteArray trimmed = body.trimmed();
  return trimmed == "MD5OK" || trimmed == "SHA1OK" || trimmed == "CRCOK";
}

} // namespace ScrapeAssetDedup
