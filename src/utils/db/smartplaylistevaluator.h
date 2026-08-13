#ifndef SMARTPLAYLISTEVALUATOR_H
#define SMARTPLAYLISTEVALUATOR_H

#include <QList>
#include <QString>

#include "smartfilter.h"

class QSqlDatabase;

/// Translates one SmartFilter::Filter into a list of (collection_uuid,
/// path) pairs by querying the `items` table on the supplied SQL database
/// connection. Pure SQL — no widget or worker-thread coupling — so the
/// QueryManager smart-playlist branch can call this directly with its
/// worker `m_db`, and the unit tests can pass an in-memory DB they
/// control.
namespace SmartPlaylistEvaluator {

/// One result row. Keyed the same way playlist_items / item_metadata /
/// item_artwork are, so the QueryManager scope-table populator can
/// INSERT rows verbatim.
struct Match {
  QString collectionUuid;
  QString path;
};

/// Evaluate the filter and return matching items. Empty list on DB error
/// (errors are logged via ErrorUtils::logError so the caller doesn't have
/// to gate every call on a Result). Caller is responsible for pre-creating
/// the items table — the evaluator does not run schema migrations.
[[nodiscard]] QList<Match> evaluate(QSqlDatabase &db, const SmartFilter::Filter &filter);

/// Evaluate a composed set and combine the per-rule results — intersection
/// for MatchMode::All, union for MatchMode::Any. Each rule runs as its own
/// query so its ordering and result cap keep their meaning; the combined list
/// follows the first rule's order. A single-rule set is exactly the overload
/// above.
[[nodiscard]] QList<Match> evaluate(QSqlDatabase &db, const SmartFilter::FilterSet &set);

} // namespace SmartPlaylistEvaluator

#endif // SMARTPLAYLISTEVALUATOR_H
