#ifndef NOINTROPARSE_H
#define NOINTROPARSE_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/// Pure parsers for the No-Intro DAT-o-MATIC daily-download flow
/// (Kartend-m6qsb.16). The live site is the only source of truth and is
/// deliberately automation-hostile, so the brittle, page-shape-dependent bits
/// are quarantined here as pure functions over captured bytes — unit-testable
/// against HTML fixtures, with zero network or Qt-widget coupling. The
/// orchestrator (NoIntroDownloader) does the 4-step request dance and leans
/// on these for every parse.
///
/// Verified flow (2026-06-13):
///   1. GET  index.php?page=download&op=daily&s=<sysId>  -> daily form
///   2. POST that form (dat_type + setN=Ok + <requestField>=Request)
///        -> 302 Location: index.php?page=manager&download=<id>
///   3. GET  index.php?page=manager&download=<id>        -> confirm token
///   4. POST that page (<token>=Download!!)              -> application/zip
namespace NoIntroParse {

/// One selectable "set" checkbox on the daily form (set1, set2, …). The label
/// is best-effort adjacent text for display; field is the POST key.
struct DailySet {
  QString field;                 ///< POST field name, e.g. "set1".
  QString label;                 ///< Human-ish adjacent text; may equal field.
  bool checkedByDefault = false; ///< Mirrors the page's checked="checked".
};

/// The parsed daily-download form.
struct DailyForm {
  bool valid = false; ///< False when the expected form wasn't found.
  /// True when the `name="daily"` form block was located on the page at all,
  /// independent of whether its contents parsed into a valid request
  /// (Kartend-s9k45). Lets a caller distinguish a likely site/layout change or
  /// anti-bot block (`formFound == false`) from a form that was found but came
  /// back without a usable Request button (`formFound == true && !valid`).
  bool formFound = false;
  QString requestField;   ///< Submit name, e.g. "daily_dl_2026-05-30".
  QString packDate;       ///< Date parsed out of requestField, e.g. "2026-05-30".
  QStringList datTypes;   ///< Offered dat_type radio values (dat/pc/db).
  QString defaultDatType; ///< The checked dat_type (falls back to "dat").
  QList<DailySet> sets;   ///< setN checkboxes in document order.
};

/// Parse the daily-download page. Returns {valid=false} when the
/// `name="daily"` form or its Request button is absent (page changed /
/// anti-bot / wrong page) so the caller can surface a clear error rather
/// than POST garbage.
[[nodiscard]] DailyForm parseDailyPage(const QByteArray &html);

/// Extract the 32-hex confirm token from the manager page's
/// `<input type="submit" name="<token>" value="Download!!">`. Empty string
/// when not found.
[[nodiscard]] QString parseConfirmToken(const QByteArray &html);

/// Pull the filename from a Content-Disposition header value
/// (`attachment; filename="...zip"`). Falls back to empty when absent or
/// unquoted-and-unparseable; the caller then names the file itself.
[[nodiscard]] QString filenameFromContentDisposition(const QString &headerValue);

} // namespace NoIntroParse

#endif // NOINTROPARSE_H
