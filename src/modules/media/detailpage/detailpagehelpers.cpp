#include "detailpagehelpers.h"

#include <QDateTime>

namespace DetailPagePayloadBuilder {

QString pickDisplayTitle(const QString &fallback, const QString &metaTitle) {
  return metaTitle.isEmpty() ? fallback : metaTitle;
}

QHash<QString, QString> buildArtworkOverrideMap(const QList<ItemArtworkStore::ItemArtwork> &rows) {
  QHash<QString, QString> out;
  out.reserve(rows.size());
  for (const auto &row : rows) {
    out.insert(row.artworkType, row.manualPath);
  }
  return out;
}

QStringList collectCustomArtworkTypes(const QList<ItemArtworkStore::ItemArtwork> &rows) {
  QStringList out;
  for (const auto &row : rows) {
    if (!ItemArtworkStore::isStandardType(row.artworkType)) {
      out.append(row.artworkType);
    }
  }
  return out;
}

FileInfoFields buildFileInfoFields(const QFileInfo &info) {
  FileInfoFields fields;
  if (!info.exists()) {
    return fields;
  }
  fields.fileSize = info.size();
  fields.fileModified = info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
  fields.fileExtension =
      info.suffix().isEmpty() ? QString() : QStringLiteral(".%1").arg(info.suffix().toLower());
  return fields;
}

} // namespace DetailPagePayloadBuilder
