#ifndef DATLOOKUP_H
#define DATLOOKUP_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include "errorutils.h"

/// Offline ROM identification via DAT files.
///
/// DAT files are XML catalogues that map ROM hashes (CRC32 / MD5 / SHA-1)
/// to canonical names + sizes. They're community-maintained for the major
/// cartridge / disc systems and let Kartend identify a ROM offline (no
/// API call) and feed the canonical name into ScreenScraper's `romnom`
/// param for a much higher API-match rate than the on-disk filename.
///
/// Scope of this module:
/// - Two XML dialects, picked automatically from the root element:
///   * **Logiqx `<datafile>`** — No-Intro, Redump, and TOSEC. `<game>`
///     children, each with one or more `<rom>` hash entries. TOSEC's
///     optional `<release>` metadata child is ignored (it carries
///     region/date info, not hashes).
///   * **MAME `<mame>` listxml** — `<machine>` children with `<rom>`
///     entries; the machine's `<description>` text is preferred over
///     the cryptic `name=` set-id when present. `<rom status="nodump">`
///     entries (placeholder for an undumped chip) are skipped since
///     their hashes are zero/empty.
/// - In-memory lookup. The on-disk sqlite cache for very large DATs
///   (multi-100k entries) is a separate follow-up — for typical ROM
///   collections (~5k–20k entries) the parse-on-load cost is sub-
///   second and not worth caching to disk.
namespace DatLookup {

/// Which catalogue dialect a given DAT was parsed as. Picked from the
/// XML root element by `detectDialect` / `parseDat`. Exposed on Store
/// so callers can surface "Loaded N entries from MAME listxml" etc.
enum class Dialect {
  Unknown, ///< Root not recognised; nothing was parsed.
  Logiqx,  ///< `<datafile>` root — No-Intro, Redump, TOSEC.
  Mame,    ///< `<mame>` root — MAME listxml.
};

/// One entry from a parsed DAT. Hash fields are stored lowercase-hex
/// regardless of the case the source XML uses, so callers can pass
/// already-normalised hex strings without per-call normalisation.
struct DatRecord {
  /// `<game name="...">` — the canonical title (typically with
  /// region tag, e.g. "Game Title (USA) (Rev 1)").
  QString gameName;
  /// `<rom name="...">` — the canonical filename including
  /// extension (e.g. "Game Title (USA).gb"). Preferred over
  /// gameName when feeding into ScreenScraper's romnom param
  /// because it already includes the extension SS expects to
  /// strip.
  QString romName;
  /// File size in bytes; -1 when the source DAT didn't declare it.
  qint64 size = -1;
  /// Lowercase hex digests. Empty when the source DAT didn't
  /// declare the hash (rare for sha1/md5, common for legacy DATs
  /// that only carry crc).
  QString crc;
  QString md5;
  QString sha1;
};

/// Peek at the XML root element to decide which parser to dispatch
/// to. Returns `Dialect::Unknown` when the bytes don't open with a
/// recognised root (typically the user pointed the picker at a
/// non-DAT XML file). Cheap — stops at the first start element.
[[nodiscard]] Dialect detectDialect(const QByteArray &xml);

/// Parse a Logiqx-shaped DAT (UTF-8 XML bytes) — `<datafile>` root
/// with `<game>` children carrying `<rom>` hashes. Covers No-Intro,
/// Redump, and TOSEC since the three cataloguers share this schema.
/// TOSEC's optional `<release>` child of `<game>` is tolerated and
/// ignored — it carries region/language metadata, not hashes.
/// Streaming parser — memory is bounded by the result list, not the
/// source buffer.
[[nodiscard]] ErrorUtils::Result<QList<DatRecord>> parseLogiqxDat(const QByteArray &xml);

/// Parse a MAME listxml file — `<mame>` root with `<machine>`
/// children. Differs from Logiqx in two ways: gameName comes from the
/// machine's `<description>` text element (e.g. "Pac-Man (Midway)")
/// when present, falling back to the `name=` set-id (e.g. "pacman")
/// otherwise; `<rom status="nodump">` placeholder entries are
/// skipped since they declare no usable hash.
[[nodiscard]] ErrorUtils::Result<QList<DatRecord>> parseMameListXml(const QByteArray &xml);

/// Auto-dispatching parser. Sniffs the root element via
/// `detectDialect` and routes to the matching parser. Returns an
/// `InvalidArgument` error when the dialect can't be identified so
/// the user gets a clearer diagnosis than a downstream parse hiccup.
[[nodiscard]] ErrorUtils::Result<QList<DatRecord>> parseDat(const QByteArray &xml);

/// Backwards-compat alias for code (and tests) that predate the
/// dialect dispatch. No-Intro and Redump are both Logiqx-shaped so
/// the underlying parser is identical.
[[nodiscard]] inline ErrorUtils::Result<QList<DatRecord>> parseNoIntroDat(const QByteArray &xml) {
  return parseLogiqxDat(xml);
}

/// Indexed store wrapping a parsed record list. Builds hash maps
/// from sha1 / md5 / crc → record pointer at construction time so
/// per-lookup cost is O(1). The store owns the records (move-in)
/// so caller hand-off is cheap; lookup returns observer pointers
/// into the owned list.
class Store {
public:
  Store() = default;
  /// Build the store from `records`. Records with no hashes at all
  /// are discarded (they wouldn't be findable). Hash collisions
  /// (two records with the same sha1, etc.) keep the first-seen
  /// record — DAT files rarely collide and when they do the user
  /// gets the older entry, which is the safer default. The optional
  /// `dialect` tag is purely informational — callers (UI status
  /// strings, etc.) can read it back via `detectedDialect()`.
  explicit Store(QList<DatRecord> records, Dialect dialect = Dialect::Unknown);

  /// Try the hash kinds in descending order of reliability:
  /// sha1 → md5 → crc. The first non-empty hash that matches an
  /// indexed record wins. Returns nullptr when nothing matches.
  [[nodiscard]] const DatRecord *lookup(const QString &md5, const QString &sha1,
                                        const QString &crc) const;

  /// Per-kind lookups, exposed for tests and for callers that
  /// already know which hash they want to query.
  [[nodiscard]] const DatRecord *lookupBySha1(const QString &sha1) const;
  [[nodiscard]] const DatRecord *lookupByMd5(const QString &md5) const;
  [[nodiscard]] const DatRecord *lookupByCrc(const QString &crc) const;

  [[nodiscard]] int recordCount() const { return static_cast<int>(m_records.size()); }
  [[nodiscard]] bool isEmpty() const { return m_records.isEmpty(); }
  [[nodiscard]] Dialect detectedDialect() const { return m_dialect; }

private:
  QList<DatRecord> m_records;
  QHash<QString, int> m_bySha1; // hash → index into m_records
  QHash<QString, int> m_byMd5;
  QHash<QString, int> m_byCrc;
  Dialect m_dialect = Dialect::Unknown;
};

/// Convenience: parse the file at `path` and return the Store.
/// Returns an error result with the file/parse error context when
/// the file can't be read or the XML is malformed.
[[nodiscard]] ErrorUtils::Result<Store> loadStoreFromFile(const QString &path);

} // namespace DatLookup

#endif // DATLOOKUP_H
