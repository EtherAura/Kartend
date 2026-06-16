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
/// - One non-XML dialect, sniffed when the bytes don't open as XML:
///   * **clrmamepro text** — the older `clrmamepro (` / `emulator (`
///     header block followed by `game (` / `resource (` blocks whose
///     `rom ( name … crc … sha1 … )` lines carry the hashes. Parsed by
///     a small tokeniser rather than streamed (these legacy catalogues
///     are far smaller than a MAME listxml).
/// - In-memory lookup. The on-disk sqlite cache for very large DATs
///   (multi-100k entries) is a separate follow-up — for typical ROM
///   collections (~5k–20k entries) the parse-on-load cost is sub-
///   second and not worth caching to disk.
namespace DatLookup {

/// Which catalogue dialect a given DAT was parsed as. Picked from the
/// XML root element by `detectDialect` / `parseDat`. Exposed on Store
/// so callers can surface "Loaded N entries from MAME listxml" etc.
enum class Dialect {
  Unknown,    ///< Root not recognised; nothing was parsed.
  Logiqx,     ///< `<datafile>` root — No-Intro, Redump, TOSEC.
  Mame,       ///< `<mame>` root — MAME listxml.
  ClrMamePro, ///< `clrmamepro (`/`emulator (` text format — older catalogues.
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
  /// Parent set this entry is a clone of (Kartend-m6qsb.13), captured from the
  /// game/machine's `cloneof` (falling back to `romof`) in MAME listxml,
  /// clrmamepro, and Logiqx exports that carry it. Empty for parent sets and
  /// for cataloguers that don't model clones (most No-Intro / Redump DATs).
  /// Carried through so future clone-aware auditing has the relationship; no
  /// current consumer interprets it.
  QString cloneOf;
  /// "Missing In Action" flag (Kartend-34lab): the entry is documented in the
  /// catalogue but known to be unshared/unavailable, marked `mia="yes"` on the
  /// `<game>`/`<machine>`/`<rom>` (Logiqx/MAME) or `mia yes` in clrmamepro.
  /// Lets the auditor surface RomVault-style salmon "MIA" counts so a missing
  /// entry that is *expected* to be missing reads differently from one the user
  /// could still obtain. Empty/false for cataloguers that don't model MIA.
  bool mia = false;
};

/// Peek at the XML root element to decide which parser to dispatch
/// to. Returns `Dialect::Unknown` when the bytes don't open with a
/// recognised root (typically the user pointed the picker at a
/// non-DAT XML file). Cheap — stops at the first start element.
[[nodiscard]] Dialect detectDialect(const QByteArray &xml);

/// Identifying metadata from a DAT's head, independent of its record list.
/// Logiqx: the `<header>` child's `<name>` / `<description>` / `<version>`
/// text elements ("what does this catalogue cover" — the signal the
/// DAT-to-collection matcher scores against). MAME listxml has no header
/// element; name is synthesised and version carries the `build` attribute.
/// Every field may be empty — `<header>` is optional in Logiqx.
struct DatHeader {
  Dialect dialect = Dialect::Unknown;
  QString name;
  QString description;
  QString version;
};

/// Header-only probe: parse just far enough to read the dialect + header,
/// then stop — never walks the record list, so calling it on a folder of
/// large DATs in a row stays cheap. Unparseable / unrecognised bytes yield
/// `{Dialect::Unknown, …empty}` rather than an error: a probe answers
/// "what is this?", and "nothing usable" is an answer, not a failure.
[[nodiscard]] DatHeader probeHeader(const QByteArray &xml);

/// File convenience for `probeHeader`. Reads a bounded prefix of the file
/// (both dialects put the metadata at the document head) so probing a
/// 100MB listxml doesn't read 100MB. Missing/unreadable file yields the
/// Unknown-dialect header.
[[nodiscard]] DatHeader probeHeaderFromFile(const QString &path);

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

/// Parse a clrmamepro text DAT — the older non-XML format whose blocks
/// look like `game ( name "…" rom ( name "…" size N crc … sha1 … ) )`.
/// The `clrmamepro (` / `emulator (` header block is skipped (its
/// fields are read by `probeHeader`); only `game` / `machine` / `set` /
/// `resource` blocks contribute records. `rom` entries flagged
/// `status nodump`, or carrying no hash at all, are dropped — same rule
/// as the XML dialects. Tokenises the whole buffer (not streamed); fine
/// for the small legacy catalogues this targets.
[[nodiscard]] ErrorUtils::Result<QList<DatRecord>> parseClrMameProDat(const QByteArray &xml);

/// Auto-dispatching parser. Sniffs the format via `detectDialect` and
/// routes to the matching parser (Logiqx / MAME listxml / clrmamepro
/// text). Returns an `InvalidArgument` error when the dialect can't be
/// identified so the user gets a clearer diagnosis than a downstream
/// parse hiccup.
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

  /// All records in the store, in original DAT order. Exposed so an
  /// audit can enumerate the full catalogue to compute the "missing"
  /// set — `lookup()` only answers "do I hold this hash?", which can't
  /// surface entries the collection lacks entirely. Records with no
  /// hashes were dropped at construction, so this is the findable
  /// subset (the same set the indexes cover).
  [[nodiscard]] const QList<DatRecord> &records() const { return m_records; }

private:
  QList<DatRecord> m_records;
  QHash<QString, int> m_bySha1; // hash → index into m_records
  QHash<QString, int> m_byMd5;
  QHash<QString, int> m_byCrc;
  Dialect m_dialect = Dialect::Unknown;
};

/// Read a DAT file's bytes, transparently unpacking a `.zip`-packed DAT
/// (Kartend-m6qsb.28): when the file opens with the PK zip magic its first
/// `.dat` member is extracted (needs an archive tool on PATH) and returned;
/// otherwise the file is read directly. `maxBytes > 0` caps the *raw* read (the
/// header probe uses this over huge listxmls) — a zip member is always returned
/// whole, since zipped catalogues are small. Returns empty for a missing /
/// unreadable file, or a zip with no extractable `.dat` member. Parsing itself
/// stays byte-pure; only this file-reading convenience knows about archives.
[[nodiscard]] QByteArray readDatFile(const QString &path, qint64 maxBytes = -1);

/// Convenience: parse the file at `path` and return the Store. Reads via
/// readDatFile, so a `.zip`-packed DAT is loaded transparently. Returns an
/// error result with the file/parse error context when the file can't be read
/// or the content is malformed.
[[nodiscard]] ErrorUtils::Result<Store> loadStoreFromFile(const QString &path);

} // namespace DatLookup

#endif // DATLOOKUP_H
