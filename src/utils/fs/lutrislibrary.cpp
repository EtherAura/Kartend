#include "lutrislibrary.h"
#include "pathutils.h"

#include <algorithm>
#include <atomic>

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace LutrisLibrary {

namespace {

QString firstExistingFile(const QStringList &candidates) {
  for (const QString &path : candidates) {
    if (QFileInfo::exists(path)) {
      return path;
    }
  }
  return {};
}

} // namespace

auto defaultDataDir() -> QString {
  const QStringList candidates = {
      QDir::homePath() + QStringLiteral("/.local/share/lutris"),
      QDir::homePath() + QStringLiteral("/.var/app/net.lutris.Lutris/data/lutris"),
  };
  for (const QString &dir : candidates) {
    if (QFileInfo::exists(dir + QStringLiteral("/pga.db"))) {
      return dir;
    }
  }
  return {};
}

auto installedGames(const QString &dataDir) -> ErrorUtils::Result<QList<Game>> {
  const QString dbPath = dataDir + QStringLiteral("/pga.db");
  if (!QFileInfo::exists(dbPath)) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Lutris database not found",
                               "LutrisLibrary::installedGames")
        .withDetails(dbPath);
  }

  // Unique connection name per call: this can run concurrently from the
  // startup-sync worker while the GUI thread probes availability, and
  // QSqlDatabase connection names are process-global.
  static std::atomic<int> connectionCounter{0};
  const QString connectionName =
      QStringLiteral("lutris_pga_%1").arg(connectionCounter.fetch_add(1));

  QList<Game> games;
  QString errorMessage;
  ErrorCode errorCode = ErrorCode::Success;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(dbPath);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) {
      errorCode = ErrorCode::DatabaseConnectionFailed;
      errorMessage = db.lastError().text();
    } else {
      QSqlQuery query(db);
      // SELECT * + record-index lookups instead of naming columns: `hidden`
      // arrived mid-0.5.x, so pinning the column list would reject older
      // (still perfectly readable) databases.
      if (!query.exec(QStringLiteral("SELECT * FROM games"))) {
        errorCode = ErrorCode::DatabaseQueryFailed;
        errorMessage = query.lastError().text();
      } else {
        const QSqlRecord record = query.record();
        const int nameCol = record.indexOf(QStringLiteral("name"));
        const int slugCol = record.indexOf(QStringLiteral("slug"));
        const int runnerCol = record.indexOf(QStringLiteral("runner"));
        const int installedCol = record.indexOf(QStringLiteral("installed"));
        const int hiddenCol = record.indexOf(QStringLiteral("hidden"));
        if (nameCol < 0 || slugCol < 0) {
          errorCode = ErrorCode::DatabaseQueryFailed;
          errorMessage = QStringLiteral("games table lacks name/slug columns");
        } else {
          while (query.next()) {
            if (installedCol >= 0 && query.value(installedCol).toInt() != 1) {
              continue;
            }
            if (hiddenCol >= 0 && query.value(hiddenCol).toInt() == 1) {
              continue;
            }
            Game game;
            game.name = query.value(nameCol).toString();
            game.slug = query.value(slugCol).toString();
            game.runner = runnerCol >= 0 ? query.value(runnerCol).toString() : QString();
            if (game.name.isEmpty() || game.slug.isEmpty()) {
              continue;
            }
            games.append(game);
          }
        }
      }
      db.close();
    }
  }
  // The QSqlDatabase handle must be out of scope before the connection is
  // removed, or Qt warns about a connection still in use.
  QSqlDatabase::removeDatabase(connectionName);

  if (errorCode != ErrorCode::Success) {
    return ErrorContext::error(errorCode, "Cannot read the Lutris database",
                               "LutrisLibrary::installedGames")
        .withDetails(QString("%1: %2").arg(dbPath, errorMessage));
  }
  std::sort(games.begin(), games.end(),
            [](const Game &a, const Game &b) { return a.name.localeAwareCompare(b.name) < 0; });
  return games;
}

auto artworkFor(const QString &dataDir, const QString &slug) -> Artwork {
  Artwork art;
  // Kartend-9guwj: slug comes from Lutris' pga.db and is interpolated as a
  // single path COMPONENT. A row with slug "../../../../home/user/Pictures/private"
  // made this read that file and the importer copy it into the collection's
  // artwork directory as the game's cover — surfacing a file the user never
  // chose to share inside the app UI. No artwork is the correct answer for a
  // slug that cannot name one.
  if (!PathUtils::isSafePathComponent(slug)) {
    return art;
  }
  art.cover = firstExistingFile({
      dataDir + QStringLiteral("/coverart/") + slug + QStringLiteral(".jpg"),
      dataDir + QStringLiteral("/coverart/") + slug + QStringLiteral(".png"),
  });
  art.banner = firstExistingFile({
      dataDir + QStringLiteral("/banners/") + slug + QStringLiteral(".jpg"),
      dataDir + QStringLiteral("/banners/") + slug + QStringLiteral(".png"),
  });
  return art;
}

} // namespace LutrisLibrary
