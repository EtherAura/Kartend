#include "mediabackendconfig.h"

#include <QtGlobal>

namespace {
// Qt reads this once, when the FFmpeg plugin builds its hardware-context
// list. See "Advanced FFmpeg Configuration" in the Qt Multimedia docs.
constexpr const char *kDecodingHwDeviceTypesVar = "QT_FFMPEG_DECODING_HW_DEVICE_TYPES";
} // namespace

namespace MediaBackendConfig {

auto decodingHwDeviceTypes() -> QByteArray {
  return QByteArrayLiteral("cuda,vaapi,d3d11va,d3d12va,dxva2,videotoolbox,mediacodec");
}

auto applyDecodingHwDeviceTypePolicy() -> bool {
  // qEnvironmentVariableIsSet() is true even for an empty value, which is the
  // documented "disable all hardware decoding" spelling — so an explicit
  // opt-out survives here rather than being overwritten with our list.
  if (qEnvironmentVariableIsSet(kDecodingHwDeviceTypesVar)) {
    return false;
  }
  qputenv(kDecodingHwDeviceTypesVar, decodingHwDeviceTypes());
  return true;
}

} // namespace MediaBackendConfig
