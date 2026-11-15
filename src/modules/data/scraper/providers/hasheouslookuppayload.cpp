#include "hasheouslookuppayload.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace HasheousLookupPayload {

ErrorUtils::Result<QByteArray> buildBody(const RomHasher::Result &hashes) {
  QJsonObject body;
  if (!hashes.md5.isEmpty()) body[QStringLiteral("MD5")] = hashes.md5;
  if (!hashes.sha1.isEmpty()) body[QStringLiteral("SHA1")] = hashes.sha1;
  if (!hashes.crc.isEmpty()) body[QStringLiteral("CRC")] = hashes.crc;

  if (body.isEmpty()) {
    return ErrorUtils::ErrorContext::error(
        ErrorUtils::ErrorCode::InvalidArgument,
        QStringLiteral("A Hasheous lookup needs at least one of MD5, SHA1 or CRC"),
        QStringLiteral("HasheousLookupPayload::buildBody"));
  }
  return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

} // namespace HasheousLookupPayload
