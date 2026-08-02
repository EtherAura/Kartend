#ifndef MEDIABACKENDCONFIG_H
#define MEDIABACKENDCONFIG_H

#include <QByteArray>
#include <QProcessEnvironment>

/// Startup policy for Qt Multimedia's FFmpeg backend.
///
/// Kartend-0vnvo: Qt's FFmpeg plugin picks a hardware-decode backend by
/// probing them in its own priority order. On a host where the Vulkan backend
/// is available — e.g. an Intel iGPU driven by Mesa alongside a discrete
/// card — it selects `vulkan`, which leaks one DRM render-node fd and one
/// Mesa disk-cache worker thread on every QMediaPlayer::setSource(). The
/// leaked devices are never released, so a long attract-mode session grows
/// without bound (578 retained fds/threads and multi-GiB RSS + swap observed).
namespace MediaBackendConfig {

/// Backends Kartend is willing to let Qt use for hardware *decoding*, in Qt's
/// documented priority-list format. This is Qt's own set of maintainer-tested
/// backends; the ones its docs list as untested (drm, opencl, qsv, vdpau and
/// the leaking vulkan) are deliberately absent. Qt skips entries that aren't
/// available on the running platform, so the single list stays portable.
[[nodiscard]] auto decodingHwDeviceTypes() -> QByteArray;

/// Publish decodingHwDeviceTypes() into the environment for the FFmpeg plugin
/// to read. Must run before the first QMediaPlayer is constructed.
///
/// An explicit QT_FFMPEG_DECODING_HW_DEVICE_TYPES from the environment always
/// wins and is left untouched, so the backend stays tunable from outside
/// (notably `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=,` to turn hardware decoding
/// off entirely). Returns true when the policy was applied, false when an
/// existing setting was honoured instead.
auto applyDecodingHwDeviceTypePolicy() -> bool;

/// Kartend-fmdq5: drop our injected backend list from a child process's
/// environment.
///
/// applyDecodingHwDeviceTypePolicy() works by putting the list in this
/// process's environment, and QProcess children inherit it — so a launched
/// Qt application would silently be held to Kartend's backend policy too.
/// That only ever affects children that are themselves Qt apps using the
/// FFmpeg media backend (non-Qt launchers never read the variable), but it is
/// coupling we did not intend to export.
///
/// An operator-supplied value is deliberately left alone: if someone set
/// QT_FFMPEG_DECODING_HW_DEVICE_TYPES themselves, children inherited it
/// before this existed and should keep inheriting it. Only the value we
/// injected is removed.
void removeInjectedDecodingHwDeviceTypes(QProcessEnvironment &env);

} // namespace MediaBackendConfig

#endif // MEDIABACKENDCONFIG_H
