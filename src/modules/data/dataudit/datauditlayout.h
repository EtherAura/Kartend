#ifndef DATAUDITLAYOUT_H
#define DATAUDITLAYOUT_H

#include <QString>
#include <QStringList>

/// Content folder-structure detection for the DAT auditor (Kartend-m6qsb.6).
///
/// A scan root's shape decides how the audit should treat it: a flat folder
/// needs no recursive walk, a nested tree does, and an archive-per-item
/// layout (one .zip per catalogue entry) needs the auditor to hash archive
/// CONTENTS rather than container bytes — auditing it by outer bytes reports
/// ~everything Unknown + Missing. Detection is a cheap sampling probe; the
/// result is a SUGGESTION the user confirms in the audit dialog, never a
/// silent behavior switch, and it is audit-side only — collection browsing
/// (folderBrowsing.includeContentSubfolders) is deliberately untouched.
namespace DatAudit {

/// The layout classes v1 distinguishes. Subfolder-per-item (multi-file sets)
/// and split/merged set semantics are tracked as follow-ups.
enum class Layout {
  Unknown,        ///< Not detected / nothing sampled / user never confirmed.
  Flat,           ///< Items sit directly in the root; subfolders are noise.
  Nested,         ///< Items spread across subfolders; recursive walk needed.
  ArchivePerItem, ///< One archive per item; audit must hash archive members.
  Mixed,          ///< No dominant shape — treated like Nested, flagged to the user.
};

/// Stable lowercase token for persistence (dat_audit_profile.detected_layout).
/// Unknown maps to the empty string — same "no value" convention as the
/// table's other TEXT columns. Unrecognised tokens parse back as Unknown.
[[nodiscard]] QString layoutToken(Layout l);
[[nodiscard]] Layout layoutFromToken(const QString &token);

/// What the probe concluded and why. `evidence` is a short human-readable
/// sentence for the confirmation banner's tooltip ("82% of 214 sampled files
/// are archives"); `confidence` is the dominant fraction backing the call.
struct LayoutDetection {
  Layout layout = Layout::Unknown;
  double confidence = 0.0;
  QString evidence;
  int sampledFiles = 0;
};

/// Sample-probe `root` and classify its shape. Bounded: stops after an
/// internal sample cap rather than walking a 100k-file library, so it is
/// cheap enough for the UI thread on local disks. When `relevantExtensions`
/// (lowercase, no dots — the collection's allow-list) is non-empty, archive
/// extensions are always counted and other sampled files only count when
/// their suffix is in the list — sidecars (artwork, notes) would otherwise
/// dominate the shape statistics. An unreadable / empty root yields Unknown.
[[nodiscard]] LayoutDetection detectLayout(const QString &root,
                                           const QStringList &relevantExtensions = {});

} // namespace DatAudit

#endif // DATAUDITLAYOUT_H
