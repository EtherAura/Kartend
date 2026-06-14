#ifndef REDUMPPARSE_H
#define REDUMPPARSE_H

#include <QByteArray>
#include <QList>
#include <QString>

/// Pure parser for the redump.org systems index (Kartend-m6qsb.23). Unlike
/// No-Intro's DAT-o-MATIC, redump.org needs no form/cookie/token: one GET of
/// http://redump.org/datfile/ returns an HTML index of /discs/system/<slug>/
/// links, and the same <slug> downloads that system's DAT at
/// /datfile/<slug>/. This parser extracts the slug→name list for the picker;
/// the brittle, page-shape-dependent bit lives here so it's fixture-testable.
namespace RedumpParse {

/// One redump.org system: the URL slug (e.g. "psx") + its display name
/// (e.g. "Sony - PlayStation").
struct System {
  QString slug;
  QString name;
};

/// Parse the systems index HTML into a slug→name list, in document order,
/// de-duplicated by slug. Returns empty when the page shape isn't recognised
/// (site change) so the caller can fall back / report rather than guess.
[[nodiscard]] QList<System> parseSystems(const QByteArray &html);

} // namespace RedumpParse

#endif // REDUMPPARSE_H
