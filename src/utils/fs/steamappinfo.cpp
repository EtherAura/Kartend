#include "steamappinfo.h"

#include <algorithm>

#include <QFile>
#include <QPair>
#include <QVariantMap>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace SteamAppInfo {

namespace {

constexpr quint32 kMagicV28 = 0x07564428;
constexpr quint32 kMagicV29 = 0x07564429;

/// Size of the fixed per-record prelude between the size field and the
/// keyvalues blob: infoState u32 + lastUpdated u32 + picsToken u64 +
/// sha1(text) 20 + changeNumber u32 + sha1(binary) 20.
constexpr qsizetype kRecordPrelude = 4 + 4 + 8 + 20 + 4 + 20;

/// Ceiling on the whole-file read (Kartend-v3u04), the counterpart to the DAT
/// path's kMaxDatBytes. Steam writes appinfo.vdf, but it sits in a
/// user-writable directory whose path Kartend accepts from configuration, so
/// the size is not ours to assume. The read cannot be streamed — the V29
/// string table is addressed by absolute offset from the tail — so the guard
/// has to be a refusal before the read, not a bound during it. It compounds:
/// parseKeyValues materialises every record into a QVariantMap, roughly 25-40x
/// the on-disk bytes, so a file well under this cap can still be the largest
/// thing in the process. A stock appinfo.vdf for a large library is tens of
/// MB; 512 MiB is the same generous margin the DAT reader allows.
constexpr qint64 kMaxAppInfoBytes = 512LL * 1024 * 1024;

quint32 readU32(const QByteArray &data, qsizetype pos) {
  return quint32(quint8(data[pos])) | quint32(quint8(data[pos + 1])) << 8 |
         quint32(quint8(data[pos + 2])) << 16 | quint32(quint8(data[pos + 3])) << 24;
}

qint64 readI64(const QByteArray &data, qsizetype pos) {
  return qint64(quint64(readU32(data, pos)) | quint64(readU32(data, pos + 4)) << 32);
}

/// NUL-terminated UTF-8 string at `pos`; advances `pos` past the NUL.
/// Returns false when no terminator exists before the end of the buffer.
bool readCString(const QByteArray &data, qsizetype &pos, QString &out) {
  const qsizetype end = data.indexOf('\0', pos);
  if (end < 0) {
    return false;
  }
  out = QString::fromUtf8(data.constData() + pos, end - pos);
  pos = end + 1;
  return true;
}

/// Recursive binary-keyvalues parser. Keys resolve through `strings` for
/// V29, inline for V28. Returns false on a malformed node (unknown type,
/// out-of-range index, truncated buffer) — the caller then skips the app.
bool parseKeyValues(const QByteArray &data, qsizetype &pos, qsizetype limit,
                    const QStringList *strings, QVariantMap &out, int depth) {
  constexpr int kMaxDepth = 32;
  if (depth > kMaxDepth) {
    return false;
  }
  while (pos < limit) {
    const quint8 type = quint8(data[pos]);
    ++pos;
    if (type == 0x08) {
      return true;
    }
    QString key;
    if (strings != nullptr) {
      if (pos + 4 > limit) {
        return false;
      }
      const quint32 index = readU32(data, pos);
      pos += 4;
      if (qsizetype(index) >= strings->size()) {
        return false;
      }
      key = strings->at(qsizetype(index));
    } else if (!readCString(data, pos, key)) {
      return false;
    }
    switch (type) {
    case 0x00: { // nested map
      QVariantMap child;
      if (!parseKeyValues(data, pos, limit, strings, child, depth + 1)) {
        return false;
      }
      out.insert(key, child);
      break;
    }
    case 0x01: { // string
      QString value;
      if (!readCString(data, pos, value)) {
        return false;
      }
      out.insert(key, value);
      break;
    }
    case 0x02: // int32
      if (pos + 4 > limit) {
        return false;
      }
      out.insert(key, qint64(qint32(readU32(data, pos))));
      pos += 4;
      break;
    case 0x03: // float32 — rare; carried as its raw bits, never consumed here
      if (pos + 4 > limit) {
        return false;
      }
      out.insert(key, qint64(readU32(data, pos)));
      pos += 4;
      break;
    case 0x07: // uint64
      if (pos + 8 > limit) {
        return false;
      }
      out.insert(key, readI64(data, pos));
      pos += 8;
      break;
    default:
      return false;
    }
  }
  return false; // ran past the record without a closing 0x08
}

/// Ints arrive as int64 variants, but Valve sometimes stores numbers as
/// strings ("1997") — accept both.
qint64 toInt64(const QVariant &value, qint64 fallback = 0) {
  bool ok = false;
  const qint64 result = value.toLongLong(&ok);
  return ok ? result : fallback;
}

/// Collects the numeric values of a map like genres {"0": 1, "1": 25} in
/// key order (keys are decimal indices).
QList<int> numericValues(const QVariantMap &map) {
  QList<QPair<int, int>> ordered;
  ordered.reserve(map.size());
  for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
    bool indexOk = false;
    const int index = it.key().toInt(&indexOk);
    const qint64 value = toInt64(it.value(), -1);
    if (indexOk && value >= 0) {
      ordered.append({index, int(value)});
    }
  }
  std::sort(ordered.begin(), ordered.end());
  QList<int> values;
  values.reserve(ordered.size());
  for (const auto &pair : ordered) {
    values.append(pair.second);
  }
  return values;
}

/// Joined names of association entries of the given type ("developer" /
/// "publisher"): associations {"0": {type, name}, "1": {...}}.
QString associationNames(const QVariantMap &associations, const QString &wantedType) {
  QStringList names;
  for (auto it = associations.constBegin(); it != associations.constEnd(); ++it) {
    const QVariantMap entry = it.value().toMap();
    if (entry.value(QStringLiteral("type")).toString() == wantedType) {
      const QString name = entry.value(QStringLiteral("name")).toString();
      if (!name.isEmpty()) {
        names.append(name);
      }
    }
  }
  return names.join(QStringLiteral(", "));
}

AppMetadata extractMetadata(const QVariantMap &root) {
  // The blob root is usually a single "appinfo" map; tolerate its absence.
  const QVariantMap appinfo = root.contains(QStringLiteral("appinfo"))
                                  ? root.value(QStringLiteral("appinfo")).toMap()
                                  : root;
  const QVariantMap common = appinfo.value(QStringLiteral("common")).toMap();
  const QVariantMap extended = appinfo.value(QStringLiteral("extended")).toMap();
  const QVariantMap associations = common.value(QStringLiteral("associations")).toMap();

  AppMetadata meta;
  meta.name = common.value(QStringLiteral("name")).toString();
  meta.type = common.value(QStringLiteral("type")).toString();
  meta.developer = extended.value(QStringLiteral("developer")).toString();
  if (meta.developer.isEmpty()) {
    meta.developer = associationNames(associations, QStringLiteral("developer"));
  }
  meta.publisher = extended.value(QStringLiteral("publisher")).toString();
  if (meta.publisher.isEmpty()) {
    meta.publisher = associationNames(associations, QStringLiteral("publisher"));
  }
  // original_release_date is the true release; steam_release_date is the
  // Steam-store debut (later for back-catalogue titles). Prefer the former.
  meta.releaseDateUtc = toInt64(common.value(QStringLiteral("original_release_date")));
  if (meta.releaseDateUtc == 0) {
    meta.releaseDateUtc = toInt64(common.value(QStringLiteral("steam_release_date")));
  }
  meta.genreIds = numericValues(common.value(QStringLiteral("genres")).toMap());
  const QVariantMap categories = common.value(QStringLiteral("category")).toMap();
  for (auto it = categories.constBegin(); it != categories.constEnd(); ++it) {
    // Keys are "category_NN" flags; the value is a truthy 1.
    if (it.key().startsWith(QLatin1String("category_")) && toInt64(it.value()) != 0) {
      bool ok = false;
      const int id = it.key().mid(9).toInt(&ok);
      if (ok) {
        meta.categoryIds.append(id);
      }
    }
  }
  std::sort(meta.categoryIds.begin(), meta.categoryIds.end());
  meta.contentDescriptorIds =
      numericValues(common.value(QStringLiteral("content_descriptors")).toMap());
  meta.controllerSupport = common.value(QStringLiteral("controller_support")).toString();
  meta.metacriticScore = int(toInt64(common.value(QStringLiteral("metacritic_score")), -1));
  meta.reviewPercentage = int(toInt64(common.value(QStringLiteral("review_percentage")), -1));

  // Exact asset filenames (possibly under hash-named subdirs) — see the
  // header. The value is a relative path from a Valve-written file, but it
  // still feeds a filesystem lookup, so traversal shapes are rejected here.
  const QVariantMap assets = common.value(QStringLiteral("library_assets_full")).toMap();
  const auto assetPath = [&assets](const QString &kind) -> QString {
    const QVariantMap image = assets.value(kind).toMap().value(QStringLiteral("image")).toMap();
    QString relative = image.value(QStringLiteral("english")).toString();
    if (relative.isEmpty() && !image.isEmpty()) {
      relative = image.constBegin().value().toString();
    }
    if (relative.startsWith(QLatin1Char('/')) || relative.contains(QLatin1String(".."))) {
      return {};
    }
    return relative;
  };
  meta.libraryCoverPath = assetPath(QStringLiteral("library_capsule"));
  meta.libraryHeroPath = assetPath(QStringLiteral("library_hero"));
  meta.libraryLogoPath = assetPath(QStringLiteral("library_logo"));
  return meta;
}

} // namespace

auto defaultAppInfoPath(const QString &steamRoot) -> QString {
  return steamRoot + QStringLiteral("/appcache/appinfo.vdf");
}

auto read(const QString &appInfoPath, const QSet<QString> &wantedAppIds)
    -> ErrorUtils::Result<QHash<QString, AppMetadata>> {
  QFile file(appInfoPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileReadError, "Cannot open appinfo.vdf",
                               "SteamAppInfo::read")
        .withDetails(appInfoPath);
  }
  if (file.size() > kMaxAppInfoBytes) {
    return ErrorContext::error(ErrorCode::ResourceLimitExceeded, "appinfo.vdf is implausibly large",
                               "SteamAppInfo::read")
        .withDetails(QStringLiteral("%1: %2 bytes exceeds the %3-byte ceiling")
                         .arg(appInfoPath)
                         .arg(file.size())
                         .arg(kMaxAppInfoBytes));
  }
  const QByteArray data = file.readAll();
  if (data.size() < 8) {
    return ErrorContext::error(ErrorCode::InvalidConfigValue, "appinfo.vdf is truncated",
                               "SteamAppInfo::read")
        .withDetails(appInfoPath);
  }
  const quint32 magic = readU32(data, 0);
  if (magic != kMagicV28 && magic != kMagicV29) {
    return ErrorContext::error(ErrorCode::InvalidConfigValue,
                               "Unsupported appinfo.vdf format version", "SteamAppInfo::read")
        .withDetails(QString("%1: magic 0x%2").arg(appInfoPath).arg(magic, 0, 16));
  }
  const bool v29 = magic == kMagicV29;

  qsizetype pos = 8;
  qsizetype recordsEnd = data.size();
  QStringList stringTable;
  if (v29) {
    if (data.size() < 16) {
      return ErrorContext::error(ErrorCode::InvalidConfigValue, "appinfo.vdf is truncated",
                                 "SteamAppInfo::read")
          .withDetails(appInfoPath);
    }
    const qint64 tableOffset = readI64(data, 8);
    pos = 16;
    // Bound-check by SUBTRACTION, never `tableOffset + 4 > data.size()`: the
    // offset is eight fully attacker-controlled bytes, so the addition
    // overflows signed 64-bit for any value near INT64_MAX and wraps negative,
    // silently passing a `>` test and letting the readU32 below index far past
    // the buffer (QByteArray::operator[] const is Q_ASSERT-only, so release
    // builds read out of bounds rather than aborting). data.size() >= 16 is
    // established above, so the subtraction cannot underflow.
    if (tableOffset < 16 || tableOffset > data.size() - 4) {
      return ErrorContext::error(ErrorCode::InvalidConfigValue,
                                 "appinfo.vdf string table offset out of range",
                                 "SteamAppInfo::read")
          .withDetails(appInfoPath);
    }
    recordsEnd = qsizetype(tableOffset);
    const quint32 count = readU32(data, qsizetype(tableOffset));
    qsizetype stringPos = qsizetype(tableOffset) + 4;
    // Clamp the reservation to what the remaining bytes could actually hold.
    // `count` is a raw u32 from the file: reserving it directly asks for up to
    // 4 Gi * sizeof(QString) (~103 GB) before the truncation check in the loop
    // can fire, turning a 24-byte hostile file into a bad_alloc abort. Every
    // entry costs at least its NUL terminator, so the remaining byte count is a
    // sound ceiling; a genuine file is unaffected because its table really does
    // carry that many bytes.
    stringTable.reserve(qMin(qsizetype(count), data.size() - stringPos));
    for (quint32 i = 0; i < count; ++i) {
      QString entry;
      if (!readCString(data, stringPos, entry)) {
        return ErrorContext::error(ErrorCode::InvalidConfigValue,
                                   "appinfo.vdf string table is truncated", "SteamAppInfo::read")
            .withDetails(appInfoPath);
      }
      stringTable.append(entry);
    }
  }

  QHash<QString, AppMetadata> apps;
  while (pos + 8 <= recordsEnd) {
    const quint32 appId = readU32(data, pos);
    if (appId == 0) {
      break;
    }
    const quint32 size = readU32(data, pos + 4);
    const qsizetype blobEnd = pos + 8 + qsizetype(size);
    if (blobEnd > recordsEnd || qsizetype(size) < kRecordPrelude) {
      break; // corrupt length — stop rather than misparse the remainder
    }
    const QString appIdString = QString::number(appId);
    if (wantedAppIds.isEmpty() || wantedAppIds.contains(appIdString)) {
      qsizetype kvPos = pos + 8 + kRecordPrelude;
      QVariantMap root;
      if (parseKeyValues(data, kvPos, blobEnd, v29 ? &stringTable : nullptr, root, 0)) {
        apps.insert(appIdString, extractMetadata(root));
      }
      // A malformed record is skipped (record boundaries stay reliable via
      // the length prefix); the rest of the file still parses.
    }
    pos = blobEnd;
  }
  return apps;
}

auto genreName(int id) -> QString {
  // Steam store genre taxonomy. Stable ids; games use 1..74, the 50s block
  // is software categories that can still appear on tools.
  switch (id) {
  case 1:
    return QStringLiteral("Action");
  case 2:
    return QStringLiteral("Strategy");
  case 3:
    return QStringLiteral("RPG");
  case 4:
    return QStringLiteral("Casual");
  case 9:
    return QStringLiteral("Racing");
  case 18:
    return QStringLiteral("Sports");
  case 23:
    return QStringLiteral("Indie");
  case 25:
    return QStringLiteral("Adventure");
  case 28:
    return QStringLiteral("Simulation");
  case 29:
    return QStringLiteral("Massively Multiplayer");
  case 37:
    return QStringLiteral("Free To Play");
  case 50:
    return QStringLiteral("Accounting");
  case 51:
    return QStringLiteral("Animation & Modeling");
  case 52:
    return QStringLiteral("Audio Production");
  case 53:
    return QStringLiteral("Design & Illustration");
  case 54:
    return QStringLiteral("Education");
  case 55:
    return QStringLiteral("Photo Editing");
  case 56:
    return QStringLiteral("Software Training");
  case 57:
    return QStringLiteral("Utilities");
  case 58:
    return QStringLiteral("Video Production");
  case 59:
    return QStringLiteral("Web Publishing");
  case 60:
    return QStringLiteral("Game Development");
  case 70:
    return QStringLiteral("Early Access");
  case 71:
    return QStringLiteral("Sexual Content");
  case 72:
    return QStringLiteral("Nudity");
  case 73:
    return QStringLiteral("Violent");
  case 74:
    return QStringLiteral("Gore");
  default:
    return {};
  }
}

auto genreNames(const QList<int> &ids) -> QStringList {
  QStringList names;
  for (const int id : ids) {
    const QString name = genreName(id);
    if (!name.isEmpty() && !names.contains(name)) {
      names.append(name);
    }
  }
  return names;
}

auto playersDescription(const QList<int> &categoryIds) -> QString {
  // Store category flags that describe player modes, in presentation order.
  // Other flags (achievements, cloud, trading cards…) are ignored.
  static const QList<QPair<int, QString>> kPlayerModes = {
      {2, QStringLiteral("Single-player")},
      {1, QStringLiteral("Multi-player")},
      {49, QStringLiteral("PvP")},
      {36, QStringLiteral("Online PvP")},
      {47, QStringLiteral("LAN PvP")},
      {37, QStringLiteral("Shared/Split Screen PvP")},
      {9, QStringLiteral("Co-op")},
      {38, QStringLiteral("Online Co-op")},
      {48, QStringLiteral("LAN Co-op")},
      {39, QStringLiteral("Shared/Split Screen Co-op")},
      {24, QStringLiteral("Shared/Split Screen")},
      {27, QStringLiteral("Cross-Platform Multiplayer")},
      {20, QStringLiteral("MMO")},
  };
  QStringList parts;
  for (const auto &mode : kPlayerModes) {
    if (categoryIds.contains(mode.first)) {
      parts.append(mode.second);
    }
  }
  return parts.join(QStringLiteral(", "));
}

auto contentDescriptorNames(const QList<int> &ids) -> QStringList {
  static const QHash<int, QString> kDescriptors = {
      {1, QStringLiteral("Some Nudity or Sexual Content")},
      {2, QStringLiteral("Frequent Violence or Gore")},
      {3, QStringLiteral("Adult Only Sexual Content")},
      {4, QStringLiteral("Frequent Nudity or Sexual Content")},
      {5, QStringLiteral("General Mature Content")},
  };
  QStringList names;
  for (const int id : ids) {
    const QString name = kDescriptors.value(id);
    if (!name.isEmpty() && !names.contains(name)) {
      names.append(name);
    }
  }
  return names;
}

} // namespace SteamAppInfo
