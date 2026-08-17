#include "searchpreset.h"

#include "view_settings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace SearchPresetIO {

namespace {

constexpr const char *KEY_NAME = "name";
constexpr const char *KEY_DESCRIPTION = "description";
constexpr const char *KEY_SCHEMA = "schemaVersion";
constexpr const char *KEY_SEARCH_TEXT = "searchText";
constexpr const char *KEY_SORT_MODE = "sortMode";
constexpr const char *KEY_EXCLUDE_SUBFOLDERS = "excludeSubfoldersFromSort";
constexpr const char *KEY_TYPE_FILTER = "collectionTypeFilter";
constexpr const char *KEY_HIDE_SUBCOLLECTIONS = "hideSubcollectionTiles";

constexpr int kCurrentSchemaVersion = 1;
// SortMode's enumerators carry explicit, documented values, which is what
// makes the raw int a safe wire format. The bound is the guard: a preset
// written by a build with more sort modes must be REFUSED, not silently
// clamped to A-Z, or the user gets a preset that quietly does the wrong thing.
constexpr int kSortModeMax = static_cast<int>(SortMode::SizeAscending);

} // namespace

QJsonObject toJson(const SearchPreset &preset) {
  QJsonObject o;
  o[KEY_NAME] = preset.name;
  o[KEY_DESCRIPTION] = preset.description;
  o[KEY_SCHEMA] = preset.schemaVersion;
  o[KEY_SEARCH_TEXT] = preset.searchText;
  o[KEY_SORT_MODE] = static_cast<int>(preset.sortMode);
  o[KEY_EXCLUDE_SUBFOLDERS] = preset.excludeSubfoldersFromSort;
  o[KEY_TYPE_FILTER] = preset.collectionTypeFilter;
  o[KEY_HIDE_SUBCOLLECTIONS] = preset.hideSubcollectionTiles;
  return o;
}

ErrorUtils::Result<SearchPreset> fromJson(const QJsonObject &obj) {
  SearchPreset preset;
  preset.name = obj.value(KEY_NAME).toString().trimmed();
  if (preset.name.isEmpty()) {
    // An unnamed preset cannot be picked, replaced, or removed — the registry
    // is name-keyed — so it is rejected at the door rather than stored as a
    // row the UI can never address.
    return ErrorContext::error(ErrorCode::InvalidArgument, "Search preset has no name",
                               "SearchPresetIO::fromJson");
  }

  preset.schemaVersion = obj.value(KEY_SCHEMA).toInt(kCurrentSchemaVersion);
  if (preset.schemaVersion > kCurrentSchemaVersion) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Search preset was written by a newer version of Kartend",
                               "SearchPresetIO::fromJson")
        .withDetails(QStringLiteral("Preset '%1' schemaVersion %2, this build understands %3")
                         .arg(preset.name)
                         .arg(preset.schemaVersion)
                         .arg(kCurrentSchemaVersion));
  }

  const int sortMode = obj.value(KEY_SORT_MODE).toInt(0);
  if (sortMode < 0 || sortMode > kSortModeMax) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Search preset has an unknown sort mode",
                               "SearchPresetIO::fromJson")
        .withDetails(QStringLiteral("Preset '%1' sortMode %2").arg(preset.name).arg(sortMode));
  }

  preset.description = obj.value(KEY_DESCRIPTION).toString();
  preset.searchText = obj.value(KEY_SEARCH_TEXT).toString();
  preset.sortMode = static_cast<SortMode>(sortMode);
  preset.excludeSubfoldersFromSort = obj.value(KEY_EXCLUDE_SUBFOLDERS).toBool(false);
  preset.collectionTypeFilter = obj.value(KEY_TYPE_FILTER).toString().trimmed();
  preset.hideSubcollectionTiles = obj.value(KEY_HIDE_SUBCOLLECTIONS).toBool(false);
  return preset;
}

void applyTo(const SearchPreset &preset, ViewSettings &target) {
  target.sortMode = preset.sortMode;
  target.excludeSubfoldersFromSort = preset.excludeSubfoldersFromSort;
  target.collectionTypeFilter = preset.collectionTypeFilter;
  target.hideSubcollectionTiles = preset.hideSubcollectionTiles;
}

SearchPreset fromViewSettings(const ViewSettings &source, const QString &searchText,
                              const QString &name) {
  SearchPreset preset;
  preset.name = name.trimmed().isEmpty() ? QStringLiteral("Current") : name.trimmed();
  preset.searchText = searchText;
  preset.sortMode = source.sortMode;
  preset.excludeSubfoldersFromSort = source.excludeSubfoldersFromSort;
  preset.collectionTypeFilter = source.collectionTypeFilter;
  preset.hideSubcollectionTiles = source.hideSubcollectionTiles;
  return preset;
}

ErrorUtils::Result<QList<SearchPreset>> loadRegistry(const QString &filePath) {
  QList<SearchPreset> out;
  QFile file(filePath);
  if (!file.exists()) {
    return out; // no registry yet is not an error — it is the first run
  }
  if (!file.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileReadError, "Could not open search-preset registry",
                               "SearchPresetIO::loadRegistry")
        .withDetails(filePath);
  }
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
  file.close();
  if (err.error != QJsonParseError::NoError || !doc.isArray()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Search-preset registry is malformed",
                               "SearchPresetIO::loadRegistry")
        .withDetails(err.errorString());
  }

  const QJsonArray arr = doc.array();
  for (const auto &value : arr) {
    if (!value.isObject()) {
      continue;
    }
    // One bad entry is SKIPPED rather than failing the load. Unlike a filter
    // rule — where dropping one changes what the query means — presets are
    // independent of each other, so losing the whole list because a single
    // hand-edited row is malformed would be the harsher outcome.
    auto parsed = fromJson(value.toObject());
    if (parsed.isOk()) {
      out.append(parsed.value());
    }
  }
  return out;
}

ErrorUtils::Result<bool> saveRegistry(const QList<SearchPreset> &presets, const QString &filePath) {
  QJsonArray arr;
  for (const SearchPreset &preset : presets) {
    arr.append(toJson(preset));
  }
  QSaveFile file(filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not write search-preset registry",
                               "SearchPresetIO::saveRegistry")
        .withDetails(filePath);
  }
  file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    return ErrorContext::error(ErrorCode::FileWriteError, "Could not commit search-preset registry",
                               "SearchPresetIO::saveRegistry")
        .withDetails(filePath);
  }
  return true;
}

QList<SearchPreset> addOrReplace(const QList<SearchPreset> &registry, const SearchPreset &preset) {
  QList<SearchPreset> out;
  out.reserve(registry.size() + 1);
  bool replaced = false;
  for (const SearchPreset &existing : registry) {
    if (existing.name.compare(preset.name, Qt::CaseInsensitive) == 0) {
      out.append(preset); // replace in place, keeping the picker's ordering
      replaced = true;
    } else {
      out.append(existing);
    }
  }
  if (!replaced) {
    out.append(preset);
  }
  return out;
}

QList<SearchPreset> removeByName(const QList<SearchPreset> &registry, const QString &name) {
  QList<SearchPreset> out;
  out.reserve(registry.size());
  for (const SearchPreset &existing : registry) {
    if (existing.name.compare(name, Qt::CaseInsensitive) != 0) {
      out.append(existing);
    }
  }
  return out;
}

} // namespace SearchPresetIO
