#ifndef UICONSTANTS_LAUNCH_H
#define UICONSTANTS_LAUNCH_H

namespace UIConstants {

// =============================================================================
// Launch
// Limits for spawning external processes and extracting archives.
// =============================================================================
namespace Launch {
/// Maximum directory depth walked when locating an extracted file inside
/// a temp extraction directory. Bounds work and limits the blast radius if
/// a malicious archive contains deeply nested or symlinked structures.
inline constexpr int MAX_EXTRACTION_DEPTH = 16;
/// Ceiling on entries read from a generated multi-disc playlist, and on the
/// playlist's own size (Kartend-ab8ri). A real release is a handful of discs;
/// these only stop a corrupt or hand-edited .m3u from driving an unbounded
/// number of extractions.
inline constexpr int MAX_PLAYLIST_ENTRIES = 64;
inline constexpr long long MAX_PLAYLIST_BYTES = 1LL * 1024 * 1024;
/// Hard ceiling on the number of files inspected while scanning an
/// extraction directory for a target extension.
inline constexpr int MAX_EXTRACTION_FILES_INSPECTED = 50000;
/// Default cap on launch-history entries before older entries are trimmed.
inline constexpr int DEFAULT_HISTORY_MAX_ENTRIES = 500;
/// Minimum configurable launch-history cap. Keeps trim from deleting the
/// whole history when a user sets a tiny value.
inline constexpr int MIN_HISTORY_MAX_ENTRIES = 10;
/// Maximum configurable launch-history cap. Bounds the per-launch trim
/// query so the journal can't grow unbounded.
inline constexpr int MAX_HISTORY_MAX_ENTRIES = 50000;
/// Free space that must remain on the extraction volume. The watchdog aborts
/// the extraction when available space falls below this, so a runaway archive
/// cannot fill the disk out from under the rest of the system.
///
/// This replaced a fixed 4 GiB cumulative-byte cap (Kartend-si0p5). That cap
/// was introduced (Kartend-ijglg) to stop a decompression bomb from
/// exhausting RAM, on the reasoning that extraction targets TMPDIR and TMPDIR
/// is tmpfs. Two things were wrong with it:
///
///   - The bound was below legitimate content. A dual-layer DVD image is
///     8.5 GB, over twice the cap, so real disc images were rejected outright.
///   - The threat it named did not reach this code. Untrusted .kart bundles
///     are unpacked by KartReader::extractTo, under its own far more generous
///     KartFormat::MAX_TOTAL_EXTRACTED_BYTES. What this cap actually gated was
///     the user launching their own media from their own library.
///
/// The genuine hazard was never the byte count but the destination: extracting
/// into tmpfs spends RAM. Extraction now targets a disk-backed directory
/// (LauncherSettings::extractionDirectory) and is bounded by free space on
/// that volume, which is the resource actually at stake.
inline constexpr long long EXTRACTION_FREE_SPACE_MARGIN_BYTES = 2LL * 1024 * 1024 * 1024;
/// Poll interval for the extraction watchdog loop. Each tick re-checks the
/// cancellation flag and the decompressed-size cap, so this bounds both the
/// cancel latency and the cap-overshoot window.
inline constexpr int EXTRACTION_WATCHDOG_POLL_MS = 250;
/// Overall extraction timeout. Historically 30s because extraction blocked
/// the GUI thread; now that it runs on a worker (Kartend-mkcak) and is
/// cancellable, the timeout only has to catch a genuinely hung extractor,
/// so it is generous enough for large legitimate archives on slow disks.
inline constexpr int EXTRACTION_TIMEOUT_MS = 120000;
/// Bounded wait for the extractor child to die after kill() on the
/// cancel / size-cap / timeout paths, and for the child to reach the
/// running state after start().
inline constexpr int EXTRACTION_KILL_GRACE_MS = 3000;
} // namespace Launch
} // namespace UIConstants

#endif
