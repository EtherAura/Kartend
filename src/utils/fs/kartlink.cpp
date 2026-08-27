#include "kartlink.h"
#include "pathutils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace KartLink {

auto isKartLinkPath(const QString &filePath) -> bool {
  return QFileInfo(filePath).suffix().compare(QLatin1String(kExtension), Qt::CaseInsensitive) == 0;
}

auto managedStubRoot() -> QString {
  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (appData.isEmpty()) {
    return {};
  }
  return QDir::cleanPath(appData + QStringLiteral("/launcher-imports"));
}

auto isManagedStubPath(const QString &stubPath) -> bool {
  const QString root = managedStubRoot();
  // An unresolvable root means nothing can be proven managed. Fail CLOSED —
  // args get dropped — rather than treating "we don't know" as "trusted".
  if (root.isEmpty() || stubPath.isEmpty()) {
    return false;
  }
  // cleanPath collapses any ../ segments BEFORE the prefix test, so a crafted
  // "<root>/steam/games/../../../home/user/evil.kartlink" cannot pass as
  // managed. Same containment shape LauncherImportService's
  // removeManagedImportDirs uses for the mirror-image question.
  const QString cleaned = QDir::cleanPath(QFileInfo(stubPath).absoluteFilePath());
  return cleaned == root || cleaned.startsWith(root + QLatin1Char('/'));
}

auto read(const QString &filePath) -> ErrorUtils::Result<LinkData> {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileReadError, "Cannot open shortcut stub",
                               "KartLink::read")
        .withDetails(filePath);
  }

  // Size-check before readAll, not after: the point is to never allocate the
  // body at all. Reported as a config-value problem rather than a read error
  // because the file opened fine — it is simply not a stub.
  if (file.size() > kMaxStubBytes) {
    return ErrorContext::error(ErrorCode::InvalidConfigValue,
                               "Shortcut stub is implausibly large", "KartLink::read")
        .withDetails(QString("%1: %2 bytes exceeds the %3-byte limit")
                         .arg(filePath)
                         .arg(file.size())
                         .arg(kMaxStubBytes));
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
