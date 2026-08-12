#include "kartlink.h"
#include "pathutils.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace KartLink {

auto isKartLinkPath(const QString &filePath) -> bool {
  return QFileInfo(filePath).suffix().compare(QLatin1String(kExtension), Qt::CaseInsensitive) == 0;
}

auto read(const QString &filePath) -> ErrorUtils::Result<LinkData> {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileReadError, "Cannot open shortcut stub",
                               "KartLink::read")
        .withDetails(filePath);
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    return ErrorContext::error(ErrorCode::InvalidConfigValue, "Shortcut stub is not valid JSON",
                               "KartLink::read")
        .withDetails(QString("%1: %2").arg(filePath, parseError.errorString()));
  }

  const QJsonObject o = doc.object();
  LinkData data;
  data.version = o["version"].toInt(1);
  data.source = o["source"].toString();
  data.target = o["target"].toString().trimmed();
  data.title = o["title"].toString();
  // Optional (Kartend-4cff2); absent in every stub written before it existed.
  // Non-string members are dropped rather than stringified: a stub with a
  // malformed arg list should lose the argument, not gain "null".
  for (const auto &arg : o["args"].toArray()) {
    if (arg.isString()) {
      data.args.append(arg.toString());
    }
  }

  if (data.target.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidConfigValue, "Shortcut stub has no launch target",
                               "KartLink::read")
        .withDetails(filePath);
  }
  return data;
}

auto write(const QString &filePath, const LinkData &data) -> bool {
  QJsonObject o;
  o["version"] = data.version;
  o["source"] = data.source;
  o["target"] = data.target;
  o["title"] = data.title;
  // Written only when non-empty, so the stubs of every existing source keep
  // their exact previous bytes and a re-sync doesn't rewrite them all.
  if (!data.args.isEmpty()) {
    QJsonArray args;
    for (const QString &arg : data.args) {
      args.append(arg);
    }
    o["args"] = args;
  }
  return PathUtils::atomicWriteFile(filePath,
                                    QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
}

} // namespace KartLink
