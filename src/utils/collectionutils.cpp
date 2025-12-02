// Collection utility functions
#include "collectionutils.h"
#include <QCryptographicHash>

namespace CollectionUtils {

QString computeCollectionUuid(const QString &name, const QString &mediaDir) {
  QByteArray norm = (name + "|" + mediaDir).trimmed().toLower().toUtf8();
  QByteArray digest =
      QCryptographicHash::hash(norm, QCryptographicHash::Sha1).toHex();
  return QString::fromLatin1(digest);
}

} // namespace CollectionUtils
