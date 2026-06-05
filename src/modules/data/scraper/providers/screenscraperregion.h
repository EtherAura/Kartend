#ifndef SCREENSCRAPERREGION_H
#define SCREENSCRAPERREGION_H

#include <QString>

/// No-Intro-style region detection from a ROM filename / basename.
/// Extracted from ScreenScraperProvider so the heuristic (pure, no
/// network) can be unit-tested directly and the provider TU stays
/// focused on the API flow.
namespace ScreenScraperRegion {

/// Detect a region tag from a ROM filename or basename, returning the
/// ScreenScraper short-name ("jp", "us", "eu", …) or an empty string when
/// nothing matches (the caller treats empty as "no override, trust SS").
///
/// When hash-based ID can't run (e.g. archive extraction timed out on a
/// multi-GB PS2 .zip), SS's filename-only match often lands on the
/// canonical (US) record even when the filename made the region explicit;
/// returning the detected short-name lets the parser put it ahead of SS's
/// matched-ROM region in the preference chain.
///
/// Recognises (in order of confidence): parenthesised full names like
/// "(Japan)", "(USA)", "(Europe)"; single-letter shorthand like "(J)",
/// "(U)", "(E)" — common in older releases; and multi-region tags like
/// "(Japan, USA)" (first token wins). Case-insensitive.
[[nodiscard]] QString detectFromFilename(const QString &filenameOrBasename);

} // namespace ScreenScraperRegion

#endif // SCREENSCRAPERREGION_H
