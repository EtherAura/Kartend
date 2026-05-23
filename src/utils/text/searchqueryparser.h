#ifndef SEARCHQUERYPARSER_H
#define SEARCHQUERYPARSER_H

#include <QString>
#include <QStringList>

/// Parses a user-typed search bar string into a free-text portion plus a
/// set of structured `key:value` filter tokens. Used by the search pipeline
/// to drive both the existing FTS / LIKE plain-text path (via `freeText`)
/// and the per-token SQL clauses applied alongside.
///
/// Recognised tokens (case-insensitive on the key):
///   played:true | played:false  — has been launched at least once.
///   missing:artwork              — no artwork file on disk.
///   has:artwork                 — convenience inverse of missing:artwork.
///   favorite:true | favorite:false — present in the reserved Favorites
///                                  playlist (or not).
///   tag:NAME                    — has NAME in its tag list (case-insensitive,
///                                  may appear multiple times to AND tags).
///
/// Unknown `key:value` tokens are collected into `unknownTokens` so callers
/// can surface a non-disruptive warning; the literal token string is
/// dropped from the free-text portion so it doesn't accidentally match
/// FTS against an item's name.
namespace SearchQueryParser {

/// Tri-state matching one of {required true, required false, no requirement}.
/// Lets callers distinguish "user typed played:true" from "user typed
/// played:false" from "user didn't mention played at all" without a separate
/// boolean-pair encoding.
enum class TriState {
  Unspecified,
  True,
  False,
};

struct SearchQuery {
  /// The portion of the input that wasn't consumed by a known token. Fed to
  /// the existing FTS / LIKE pipeline so plain searches keep working.
  QString freeText;
  TriState played = TriState::Unspecified;
  TriState favorite = TriState::Unspecified;
  /// `missing:artwork` -> True; `has:artwork` -> False. Default Unspecified.
  TriState missingArtwork = TriState::Unspecified;
  /// Tag names the user asked for (case-insensitive). Multiple `tag:` tokens
  /// AND together — the result set must include all of them.
  QStringList requiredTags;
  /// Unknown `key:value` tokens the parser skipped. Empty when the query
  /// only contains recognised tokens / plain text. Callers can log these
  /// for a non-disruptive warning surface.
  QStringList unknownTokens;
};

/// Parses `raw`. Tokens are recognised when:
///   - the segment matches `key:value` (one colon, no internal whitespace),
///   - `key` (case-insensitively) is one of the known keys,
///   - `value` is non-empty.
/// All other whitespace-separated segments — including unrecognised key:value
/// pairs — are stripped from the free-text portion so they don't bleed into
/// FTS. Unrecognised tokens go into `unknownTokens` for warning surfaces.
[[nodiscard]] SearchQuery parse(const QString &raw);

} // namespace SearchQueryParser

#endif // SEARCHQUERYPARSER_H
