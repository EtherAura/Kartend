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

void removeInjectedDecodingHwDeviceTypes(QProcessEnvironment &env) {
  const QString key = QString::fromLatin1(kDecodingHwDeviceTypesVar);
  // Identify our value by comparing it, rather than by remembering that we set
  // it. A "did we inject?" flag would be process-global sticky state that
  // nothing can reset, which makes the behaviour order-dependent and awkward
  // to test; the value itself carries the same information.
  //
  // Edge case, deliberately accepted: an operator who sets exactly our list by
  // hand gets it scrubbed from children too. The child then falls back to Qt's
  // own backend selection, which is what it would have done without Kartend in
  // the picture at all.
  if (env.value(key) == QString::fromLatin1(decodingHwDeviceTypes())) {
    env.remove(key);
  }
}

} // namespace MediaBackendConfig
