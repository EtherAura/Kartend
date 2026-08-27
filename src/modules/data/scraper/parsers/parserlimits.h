#ifndef PARSERLIMITS_H
#define PARSERLIMITS_H

#include <algorithm>

#include <QSet>
#include <QString>
#include <QStringList>

/// Bounds shared by the scraper response parsers (Kartend-v3u04).
///
/// Every parser here runs on the main thread against a body that is only as
/// trustworthy as the provider that sent it — and the size guard upstream is
/// generous (HttpClient's cap is measured in MiB, because real ScreenScraper
/// media listings are large). A body full of minimal JSON objects therefore
/// reaches a parser as an array with a very large length, and two things used
/// to scale with that length unchecked: a reserve() sized straight from it,
/// and a linear QStringList::contains inside the loop walking it — quadratic
/// work on the thread that draws the UI, mid-batch.
///
/// The bounds below are deliberately far above anything a real response
/// carries (searches ask for 10 results; a release has a handful of genres),
/// so nothing legitimate is truncated. They exist to make the worst case
/// finite, not to shape normal output.
namespace ScraperParsers {

/// Most candidates one search response may contribute. Providers are asked
/// for ~10; this is two orders of magnitude of headroom for a provider that
/// ignores the limit, and still bounded.
inline constexpr qsizetype kMaxCandidates = 256;

/// Most items one joined field (genres, authors, labels, publishers …) may
/// carry. Sibling of scrapepersistence's kMaxCustomFields = 64, which bounds
/// the same class of data one layer down at persistence time.
inline constexpr qsizetype kMaxJoinItems = 64;

/// Slots to pre-allocate for @p count incoming items, never more than @p cap.
/// The point is that the allocation cannot be sized by the response alone:
/// growth past the cap still happens if the items turn out to be real, it
/// just happens incrementally instead of up front.
[[nodiscard]] inline qsizetype boundedReserve(qsizetype count, qsizetype cap) {
  return std::clamp<qsizetype>(count, 0, cap);
}

/// Collects unique strings in first-seen order, bounded to a maximum count.
///
/// Replaces the `if (!list.contains(s)) list.append(s)` idiom the parsers
/// used, which is a linear scan per item and so quadratic over an array whose
/// length the response controls. Membership is a hash lookup here, and the
/// ordered list is kept alongside because these fields are user-visible and
/// provider order is meaningful (primary author first, and so on).
class BoundedUniqueStrings {
public:
  explicit BoundedUniqueStrings(qsizetype limit = kMaxJoinItems) : m_limit(limit) {}

  /// Appends @p value unless it is empty, already present, or the limit is
  /// reached. Returns false once full, so a caller walking a huge array can
  /// stop reading it entirely rather than keep parsing into a full sink.
  bool add(const QString &value) {
    if (m_ordered.size() >= m_limit) {
      return false;
    }
    if (value.isEmpty() || m_seen.contains(value)) {
      return true;
    }
    m_seen.insert(value);
    m_ordered.append(value);
    return true;
  }

  [[nodiscard]] bool isFull() const { return m_ordered.size() >= m_limit; }
  [[nodiscard]] bool isEmpty() const { return m_ordered.isEmpty(); }
  [[nodiscard]] const QStringList &values() const { return m_ordered; }
  [[nodiscard]] QString join(const QString &separator) const { return m_ordered.join(separator); }

private:
  qsizetype m_limit;
  QSet<QString> m_seen;
  QStringList m_ordered;
};

} // namespace ScraperParsers

#endif // PARSERLIMITS_H
