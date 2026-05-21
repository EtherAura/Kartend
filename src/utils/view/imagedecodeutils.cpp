#include "imagedecodeutils.h"

#include "uiconstants/artwork.h"

#include <QImageReader>

namespace ImageDecodeUtils {

QImage loadCapped(const QString &path) {
  QImageReader reader(path);
  reader.setAutoTransform(true);
  reader.setAllocationLimit(UIConstants::Artwork::MAX_DECODE_MB);
  return reader.read();
}

} // namespace ImageDecodeUtils
