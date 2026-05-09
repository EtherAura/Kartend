#ifndef KARTCOMPRESSION_H
#define KARTCOMPRESSION_H

#include <QByteArray>

#include "errorutils.h"
#include "kartformat.h"

namespace KartCompression {

[[nodiscard]] bool zstdAvailable();

[[nodiscard]] ErrorUtils::Result<QByteArray>
decompress(const QByteArray &compressed, KartFormat::Compression algo, qint64 expectedSize);

[[nodiscard]] ErrorUtils::Result<QByteArray> compress(const QByteArray &raw,
                                                      KartFormat::Compression algo);

} // namespace KartCompression

#endif
