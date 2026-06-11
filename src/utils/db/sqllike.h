#ifndef KARTEND_UTILS_DB_SQLLIKE_H
#define KARTEND_UTILS_DB_SQLLIKE_H

// Shared LIKE-pattern escaping (Kartend-joird). SQLite LIKE treats % and _
// as wildcards in the BOUND pattern too — binding alone does not neutralise
// them. User- and filesystem-derived needles (search text, subfolder names,
// smart-playlist titles, tags) routinely contain underscores, so an
// unescaped `my_videos/%` prefix also matched `myXvideos/...`, and a literal
// % matched everything. Every LIKE whose needle is not an intentional
// wildcard pattern must bind escapeLike(needle) AND append kLikeEscape to
// the SQL. Deliberate wildcard surfaces (smart-playlist path globs, the
// literal '%/%' depth probe) are exempt.

#include <QString>

namespace KartendDb {

/// Escapes \ % _ in @p needle for use inside a LIKE pattern that carries
/// `ESCAPE '\'` (append kLikeEscape to the SQL). Compose wildcards around
/// the escaped needle, e.g. "%" + escapeLike(text) + "%".
[[nodiscard]] inline QString escapeLike(QString needle) {
  needle.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
      .replace(QLatin1Char('%'), QLatin1String("\\%"))
      .replace(QLatin1Char('_'), QLatin1String("\\_"));
  return needle;
}

/// SQL fragment to append directly after the LIKE placeholder:
/// "... LIKE ?" + kLikeEscape
inline constexpr const char *kLikeEscape = " ESCAPE '\\'";

} // namespace KartendDb

#endif // KARTEND_UTILS_DB_SQLLIKE_H
