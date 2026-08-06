#ifndef KARTEND_UTILS_FS_STEAMAPPINFO_H
#define KARTEND_UTILS_FS_STEAMAPPINFO_H

#include "errorutils.h"
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

/// Read-only parser for Steam's local metadata cache —
/// `<steamRoot>/appcache/appinfo.vdf`, the *binary* VDF the client keeps for
/// every app it has seen. This is what lets an imported Steam collection get
/// developer / publisher / genres / release date / co-op flags with no
/// network and no scraping (Kartend-11elw): the store already delivered the
/// data to the client.
///
/// Two on-disk versions are supported, distinguished by magic:
///   0x07564428 (V28, Dec 2022+) — keys are inline NUL-terminated strings.
///   0x07564429 (V29, mid 2024+) — keys are u32 indices into a string table
///                                 appended to the file.
/// Layout (both): u32 magic, u32 universe, [V29: i64 stringTableOffset],
/// then app records — u32 appid (0 terminates), u32 size, a 60-byte prelude
/// (infoState, lastUpdated, picsToken, sha1(text), changeNumber,
/// sha1(binary)), and a binary-keyvalues blob (node types: 0x00 map,
/// 0x01 string, 0x02 int32, 0x03 float32, 0x07 uint64, 0x08 map end).
///
/// Notably NOT in appinfo.vdf: store descriptions. Those stay a scraper
/// concern; every field here fills what it can and leaves the rest empty.
namespace SteamAppInfo {

/// Metadata extracted from one app's `appinfo/common` + `appinfo/extended`
/// sections. Every field is optional — pre-2013 titles lack a release date,
/// tools lack almost everything.
struct AppMetadata {
  QString name;
  QString type; ///< Verbatim: "game" / "Game" / "Tool" / "Application" / "Demo" / "Music" …
  QString developer;
  QString publisher;
  qint64 releaseDateUtc =
      0; ///< Unix seconds (original_release_date, else steam_release_date); 0 = unknown.
  QList<int> genreIds;             ///< Store genre taxonomy ids (see genreName).
  QList<int> categoryIds;          ///< `category_NN` flag ids (see playersDescription).
  QList<int> contentDescriptorIds; ///< Mature-content descriptor ids 1..5.
  QString controllerSupport;       ///< "full" / "partial" / empty.
  int metacriticScore = -1;
  int reviewPercentage = -1; ///< Steam user reviews, percent positive.

  /// Exact librarycache asset paths from `common/library_assets_full`,
  /// relative to `librarycache/<appid>/` (may include a hash-named
  /// subdirectory — newer Steam stores localized/updated assets that way,
  /// which filename guessing misses). English variant preferred, first
  /// available language otherwise; empty when the section is absent.
  /// Traversal-unsafe values ("..", absolute) are rejected at parse time.
  QString libraryCoverPath; ///< library_capsule image (portrait 600x900).
  QString libraryHeroPath;  ///< library_hero image (wide banner).
  QString libraryLogoPath;  ///< library_logo image (transparent).

  /// True when Steam classifies the app as an actual game (as opposed to
  /// Proton builds, runtimes, soundtracks, tools) — a sturdier non-game
  /// filter than name matching.
  [[nodiscard]] bool isGameType() const {
    return type.compare(QLatin1String("game"), Qt::CaseInsensitive) == 0;
  }
};

/// `<steamRoot>/appcache/appinfo.vdf`.
[[nodiscard]] QString defaultAppInfoPath(const QString &steamRoot);

/// Parses the file and returns metadata keyed by appid (as a string, the
/// same form SteamLibrary::Game::appId uses). When `wantedAppIds` is
/// non-empty, other apps' records are skipped wholesale (cheap — records
/// are length-prefixed). Errors only for an unreadable file or an unknown
/// format version; a single malformed app record is skipped, not fatal.
[[nodiscard]] ErrorUtils::Result<QHash<QString, AppMetadata>>
read(const QString &appInfoPath, const QSet<QString> &wantedAppIds);

/// Store genre taxonomy id → English display name; empty for unknown ids.
[[nodiscard]] QString genreName(int id);
/// Mapped names for the known ids, unknown ids skipped, order preserved.
[[nodiscard]] QStringList genreNames(const QList<int> &ids);

/// Human "players" summary from category flags — e.g.
/// "Single-player, Co-op, Online Co-op". Empty when no player-mode
/// category is present. Canonically ordered regardless of input order.
[[nodiscard]] QString playersDescription(const QList<int> &categoryIds);

/// Mature-content descriptor names for known ids (1..5), unknown skipped.
[[nodiscard]] QStringList contentDescriptorNames(const QList<int> &ids);

} // namespace SteamAppInfo

#endif
