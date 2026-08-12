#include "itchlibrary.h"

#include <algorithm>
#include <atomic>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ItchLibrary {

namespace {

QString databasePath(const QString &configDir) {
  return configDir + QStringLiteral("/db/butler.db");
}

/// itch classifies every page it hosts; only some of them are games. An empty
/// or unknown classification is kept — a cave exists because the user
/// installed something, and a missing metadata row is not evidence against it.
bool isGameClassification(const QString &classification) {
  if (classification.isEmpty()) {
    return true;
  }
  return classification == QLatin1String("game") || classification == QLatin1String("game_mod");
}

} // namespace

auto defaultConfigDir() -> QString {
  const QStringList candidates = {
      QDir::homePath() + QStringLiteral("/.config/itch"),
      QDir::homePath() + QStringLiteral("/.var/app/io.itch.itch/config/itch"),
  };
  for (const QString &dir : candidates) {
    if (QFileInfo::exists(databasePath(dir))) {
      return dir;
    }
  }
  return {};
}

auto installedGames(const QString &configDir) -> ErrorUtils::Result<QList<Game>> {
  const QString dbPath = databasePath(configDir);
  if (!QFileInfo::exists(dbPath)) {
    return ErrorContext::error(ErrorCode::FileNotFound, "itch database not found",
                               "ItchLibrary::installedGames")
        .withDetails(dbPath);
  }

  // Unique connection name per call: this can run concurrently from the
  // startup-sync worker while the GUI thread probes availability, and
  // QSqlDatabase connection names are process-global.
  static std::atomic<int> connectionCounter{0};
  const QString connectionName =
      QStringLiteral("itch_butler_%1").arg(connectionCounter.fetch_add(1));

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
      // Two SELECT * reads and an in-memory join rather than one SQL join:
      // butler is a moving target that has renamed and added columns across
      // versions, so naming columns up front would reject databases that are
      // perfectly readable, and record-index lookups degrade per column
      // instead (mirrors LutrisLibrary).
      QHash<QString, QPair<QString, QString>> titlesById; // game id -> (title, classification)
      QSqlQuery gamesQuery(db);
      if (gamesQuery.exec(QStringLiteral("SELECT * FROM games"))) {
        const QSqlRecord record = gamesQuery.record();
        const int idCol = record.indexOf(QStringLiteral("id"));
        const int titleCol = record.indexOf(QStringLiteral("title"));
        const int classCol = record.indexOf(QStringLiteral("classification"));
        while (idCol >= 0 && gamesQuery.next()) {
          titlesById.insert(gamesQuery.value(idCol).toString(),
                            {titleCol >= 0 ? gamesQuery.value(titleCol).toString() : QString(),
                             classCol >= 0 ? gamesQuery.value(classCol).toString() : QString()});
        }
      }

      QSqlQuery cavesQuery(db);
      if (!cavesQuery.exec(QStringLiteral("SELECT * FROM caves"))) {
        errorCode = ErrorCode::DatabaseQueryFailed;
        errorMessage = cavesQuery.lastError().text();
      } else {
        const QSqlRecord record = cavesQuery.record();
        const int idCol = record.indexOf(QStringLiteral("id"));
        const int gameIdCol = record.indexOf(QStringLiteral("game_id"));
        const int folderCol = record.indexOf(QStringLiteral("install_folder_name"));
        if (idCol < 0) {
          errorCode = ErrorCode::DatabaseQueryFailed;
          errorMessage = QStringLiteral("caves table lacks an id column");
        } else {
          while (cavesQuery.next()) {
            Game game;
            game.caveId = cavesQuery.value(idCol).toString();
            if (game.caveId.isEmpty()) {
              continue;
            }
            const auto metadata = gameIdCol >= 0
                                      ? titlesById.value(cavesQuery.value(gameIdCol).toString())
                                      : QPair<QString, QString>{};
            if (!isGameClassification(metadata.second)) {
              continue;
            }
            game.title = metadata.first;
            if (game.title.isEmpty() && folderCol >= 0) {
              // Better than the cave's uuid when the metadata row is gone.
              game.title = cavesQuery.value(folderCol).toString();
            }
            if (game.title.isEmpty()) {
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
    return ErrorContext::error(errorCode, "Cannot read the itch database",
                               "ItchLibrary::installedGames")
        .withDetails(QString("%1: %2").arg(dbPath, errorMessage));
  }
  std::sort(games.begin(), games.end(),
            [](const Game &a, const Game &b) { return a.title.localeAwareCompare(b.title) < 0; });
  return games;
}

auto launchUri(const Game &game) -> QString {
  return QStringLiteral("itch://caves/") + game.caveId + QStringLiteral("/launch");
}

} // namespace ItchLibrary
