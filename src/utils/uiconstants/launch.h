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
/// Hard ceiling on cumulative decompressed bytes written by a launch-time
/// archive extraction (Kartend-ijglg). TMPDIR is tmpfs on most Linux
/// systems, so an unbounded extraction (a zip bomb inside an imported
/// archive) is a RAM/OOM DoS, not just a disk-space leak. 4 GiB comfortably
/// covers real media archives (dual-layer DVD images) while staying well
/// below typical tmpfs limits (half of RAM).
inline constexpr long long MAX_EXTRACTION_BYTES = 4LL * 1024 * 1024 * 1024;
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
