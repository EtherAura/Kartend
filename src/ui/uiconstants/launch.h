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
} // namespace Launch
} // namespace UIConstants

#endif
