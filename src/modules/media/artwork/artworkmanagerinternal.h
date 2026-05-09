#ifndef ARTWORKMANAGERINTERNAL_H
#define ARTWORKMANAGERINTERNAL_H

// Internal helpers shared across ArtworkManager translation units.
// Kept out of the public artworkmanager.h to avoid leaking implementation
// details. Only included by sibling TUs in src/modules/artwork/.

#include <QImage>
#include <QString>

/// Decode an artwork file from disk and return a DPR-aware, scaled QImage
/// sized for the artwork box. Returns a null QImage on failure or empty path.
/// Thread-safe (uses only QImageReader + QGuiApplication::primaryScreen).
auto loadAndProcessImage(const QString &path) -> QImage;

#endif // ARTWORKMANAGERINTERNAL_H
