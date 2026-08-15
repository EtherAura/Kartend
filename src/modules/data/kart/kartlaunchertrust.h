#ifndef KARTEND_MODULES_DATA_KART_KARTLAUNCHERTRUST_H
#define KARTEND_MODULES_DATA_KART_KARTLAUNCHERTRUST_H

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

#include "collection/collectionconfig.h"

/// Trust classification for the launcher block an imported .kart carries.
///
/// The governing rule (Kartend-kxqqf): **an imported bundle's launcher
/// configuration is untrusted input, because it chooses both the program
/// Kartend will start and the argv it will be started with.** The argv-array
/// launch design that protects every other path is no defence here — an
/// attacker who picks the program can simply pick a shell.
///
/// So the model is disclosure-first, not allowlist-first:
///
///   • Every non-empty launcher field the manifest supplies produces a
///     finding, so the user is always shown what will run before the import
///     commits. Nothing is silently accepted.
///   • The safe-prefix allowlist no longer decides *whether* the user is
///     asked. It only decides how loud the finding is — one of the
///     escalation reasons below.
///
/// The escalation reasons are heuristics and are deliberately not the
/// guarantee: a value that trips none of them is still disclosed. Adding a
/// reason makes a warning louder; it never makes one disappear.
namespace kart {

/// Field+path pair returned by collectSuspiciousKartPaths /
/// collectSuspiciousAssetPaths, and the row type the interactive
/// confirmation hook renders.
using SuspiciousKartPath = QPair<QString, QString>;

/// Why an imported launcher field was surfaced.
enum class LauncherTrustReason {
  /// The bundle supplies this value and nothing further was detected. Still
  /// reported — a bundle-chosen program plus a bundle-chosen argv is
  /// executable content whatever directory it lives in.
  BundleSupplied,
  /// A path outside the safe-prefix allowlist that no existing collection
  /// already trusts.
  OutsideAllowlist,
  /// The program's basename is a shell / interpreter / privilege or
  /// environment wrapper, so its arguments *are* the payload. Elevated
  /// regardless of directory: /usr/bin/sh and /bin/sh are the same binary on
  /// a merged-usr system, and an allowlisted directory says nothing about
  /// what the program does with its argv.
  InterpreterProgram,
  /// The launch parameters contain a flag whose documented job is to execute
  /// an inline string (`-c`, `--eval`, `--command=`, `/c`, …).
  InlineCodeParameter,
  /// The value resolves inside the extracted kart tree, i.e. the bundle ships
  /// the thing it wants run. Only detectable post-extraction, so callers pass
  /// the extraction root once they have it.
  BundledExecutable,
};

/// One reported launcher field. @p value is carried verbatim so the UI can
/// show the user exactly what the manifest asked for.
struct KartLauncherFinding {
  QString field;
  QString value;
  LauncherTrustReason reason = LauncherTrustReason::BundleSupplied;

  bool operator==(const KartLauncherFinding &other) const = default;
};

/// True for every reason except BundleSupplied — i.e. "a danger signal
/// matched", which the UI renders more loudly than plain disclosure.
[[nodiscard]] bool isElevatedLauncherTrustReason(LauncherTrustReason reason);

/// Short translated label for a reason. Used by the preflight dialog's status
/// column and by the confirmation prompt's row list.
[[nodiscard]] QString launcherTrustReasonLabel(LauncherTrustReason reason);

/// Report every non-empty launcher-execution field in @p cfg: launcherPath,
/// corePath and launchParameters for the primary launcher and for each
/// additional launcher. @p trustedLauncherPaths (launcher paths already
/// configured on an existing collection) suppresses the OutsideAllowlist
/// escalation only — a previously-approved path is still disclosed, and a
/// previously-approved *interpreter* stays elevated, because the argv beside
/// it is new every time. @p extractedRoot enables the BundledExecutable
/// escalation; pass an empty string pre-extraction.
[[nodiscard]] QList<KartLauncherFinding>
collectLauncherTrustFindings(const CollectionConfig &cfg, const QSet<QString> &trustedLauncherPaths,
                             const QString &extractedRoot = QString());

/// The non-launcher half of the allowlist check: collectionIcon and
/// placeholderArtwork. Split out from collectSuspiciousKartPaths so callers
/// that classify launcher fields through collectLauncherTrustFindings don't
/// report the same field twice.
[[nodiscard]] QList<SuspiciousKartPath> collectSuspiciousAssetPaths(const CollectionConfig &cfg);

/// Classify the imported manifest's externally-controlled path fields against
/// a safe-prefix allowlist (user home, /usr/bin, /usr/local/bin, /opt) plus
/// the @p trustedLauncherPaths set. Returns the (field, path) pairs that fall
/// outside any safe root. Retained as the audit-log floor in finalizeImport;
/// interactive callers use collectLauncherTrustFindings, which subsumes it
/// for launcher fields and does not depend on the allowlist to decide whether
/// to speak up.
[[nodiscard]] QList<SuspiciousKartPath>
collectSuspiciousKartPaths(const CollectionConfig &cfg, const QSet<QString> &trustedLauncherPaths);

/// Kartend-u8wf0: return the launcher fields whose value resolves to a
/// location INSIDE the extracted kart tree (@p extractedRoot) — the bundle
/// shipping the code it wants run. Covers launcherPath, corePath and any
/// path-shaped token in launchParameters: a core is dlopen()ed rather than
/// exec()ed, so the non-executable permissions extraction gives payload files
/// do not stand in its way.
[[nodiscard]] QList<SuspiciousKartPath> collectInTreeLauncherPaths(const CollectionConfig &cfg,
                                                                   const QString &extractedRoot);

/// Flatten launcher findings and suspicious asset paths into the (field,
/// value) rows the interactive confirmation hook renders. The reason label is
/// folded into the field column so the prompt says why each row is there.
[[nodiscard]] QList<SuspiciousKartPath>
importConfirmationRows(const QList<KartLauncherFinding> &findings,
                       const QList<SuspiciousKartPath> &suspiciousAssets);

} // namespace kart

#endif // KARTEND_MODULES_DATA_KART_KARTLAUNCHERTRUST_H
