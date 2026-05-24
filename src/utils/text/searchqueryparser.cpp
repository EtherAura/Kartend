#include "searchqueryparser.h"

#include <QSet>

namespace SearchQueryParser {

namespace {

TriState parseBoolValue(const QString &value, bool &ok) {
  const QString lower = value.toLower();
  if (lower == QLatin1String("true") || lower == QLatin1String("yes") ||
      lower == QLatin1String("1")) {
    ok = true;
    return TriState::True;
  }
  if (lower == QLatin1String("false") || lower == QLatin1String("no") ||
      lower == QLatin1String("0")) {
    ok = true;
    return TriState::False;
  }
  ok = false;
  return TriState::Unspecified;
}

/// Returns true when `segment` was consumed as a known token. False means
/// the caller should keep it in the free-text portion (either it wasn't
/// shaped like a token, or its key was unknown and we already logged it
/// into unknownTokens before returning false).
bool absorbToken(const QString &segment, SearchQuery &out) {
  const int colon = segment.indexOf(QLatin1Char(':'));
  if (colon <= 0 || colon == segment.size() - 1) {
    // No colon, leading colon, or trailing colon — not a token.
    return false;
  }
  const QString key = segment.left(colon).toLower();
  const QString value = segment.mid(colon + 1);
  if (value.isEmpty()) {
    return false;
  }

  // Known boolean tokens.
  if (key == QLatin1String("played")) {
    bool ok = false;
    const TriState parsed = parseBoolValue(value, ok);
    if (!ok) {
      out.unknownTokens.append(segment);
      return true; // consumed (don't leak the bad token into freeText)
    }
    out.played = parsed;
    return true;
  }
  if (key == QLatin1String("favorite") || key == QLatin1String("favourite")) {
    bool ok = false;
    const TriState parsed = parseBoolValue(value, ok);
    if (!ok) {
      out.unknownTokens.append(segment);
      return true;
    }
    out.favorite = parsed;
    return true;
  }

  // `missing:artwork` and `has:artwork` are paired tokens on a different
  // axis from the boolean ones — the user types the verb, not a true/false.
  if (key == QLatin1String("missing")) {
    if (value.toLower() == QLatin1String("artwork")) {
      out.missingArtwork = TriState::True;
      return true;
    }
    out.unknownTokens.append(segment);
    return true;
  }
  if (key == QLatin1String("has")) {
    if (value.toLower() == QLatin1String("artwork")) {
      out.missingArtwork = TriState::False;
      return true;
    }
    out.unknownTokens.append(segment);
    return true;
  }

  if (key == QLatin1String("tag")) {
    out.requiredTags.append(value);
    return true;
  }

  // Unknown key — record so callers can surface a warning, but consume the
  // segment so it doesn't leak into FTS (which would otherwise try to match
  // the literal "color:red" against item names).
  out.unknownTokens.append(segment);
  return true;
}

} // namespace

SearchQuery parse(const QString &raw) {
  SearchQuery out;
  const QString trimmed = raw.trimmed();
  if (trimmed.isEmpty()) {
    return out;
  }

  // Split on ASCII whitespace. Quoted strings aren't supported in the
  // first pass — `tag:multi word` would be parsed as `tag:multi` + free
  // text "word", which matches the lowest-friction shell-style behaviour.
  const QStringList segments = trimmed.split(QChar::Space, Qt::SkipEmptyParts);
  QStringList freeTextParts;
  freeTextParts.reserve(segments.size());

  // Track required tags case-insensitively so the same tag typed twice
  // doesn't get duplicated in the WHERE clause.
  QSet<QString> tagSeen;

  for (const QString &segment : segments) {
    SearchQuery scratch;
    if (absorbToken(segment, scratch)) {
      // Merge scratch into out, deduplicating tags case-insensitively.
      if (scratch.played != TriState::Unspecified) {
        out.played = scratch.played;
      }
      if (scratch.favorite != TriState::Unspecified) {
        out.favorite = scratch.favorite;
      }
      if (scratch.missingArtwork != TriState::Unspecified) {
        out.missingArtwork = scratch.missingArtwork;
      }
      for (const QString &t : scratch.requiredTags) {
        const QString lower = t.toLower();
        if (!tagSeen.contains(lower)) {
          tagSeen.insert(lower);
          out.requiredTags.append(t);
        }
      }
      out.unknownTokens.append(scratch.unknownTokens);
    } else {
      freeTextParts.append(segment);
    }
  }
  out.freeText = freeTextParts.join(QLatin1Char(' '));
  return out;
}

} // namespace SearchQueryParser
