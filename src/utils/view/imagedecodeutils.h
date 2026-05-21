#ifndef IMAGEDECODEUTILS_H
#define IMAGEDECODEUTILS_H

#include <QImage>
#include <QString>

/// Image-decode helpers that enforce the project's explicit allocation
/// ceiling (UIConstants::Artwork::MAX_DECODE_MB) rather than Qt's default
/// 128 MB cap. Use these instead of QImage(path) / QPixmap(path) at any
/// disk-read decode site so a malicious or corrupt image can't blow up
/// resident memory.
namespace ImageDecodeUtils {

/// Decode an image file under the project's MAX_DECODE_MB allocation
/// ceiling. Honors EXIF orientation via QImageReader::setAutoTransform.
/// Returns a null QImage on error / oversize / unsupported format.
[[nodiscard]] QImage loadCapped(const QString &path);

} // namespace ImageDecodeUtils

#endif // IMAGEDECODEUTILS_H
