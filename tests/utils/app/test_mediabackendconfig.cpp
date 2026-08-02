// Tests for the FFmpeg hardware-decode backend policy (Kartend-0vnvo).
//
// The bug: Qt's FFmpeg plugin auto-selects the `vulkan` hardware-decode
// backend when it is available, and that backend leaks one DRM render-node fd
// plus one Mesa disk-cache worker thread on every QMediaPlayer::setSource().
// Measured on the reporting host at +1 leaked fd per source change on a single
// reused player, reaching 578 retained fds/threads and multi-GiB RSS + swap
// after an attract-mode session.
//
// Exercising the actual leak needs a GPU, a real decode pipeline and thousands
// of source switches, so it cannot run in CI. What is pinned here is the
// policy that keeps Kartend off the leaking backend: the published device-type
// list must exclude the backends Qt documents as untested (vulkan above all),
// and an explicit operator override must survive untouched.
#include "mediabackendconfig.h"

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTest>

namespace {
constexpr const char *kVar = "QT_FFMPEG_DECODING_HW_DEVICE_TYPES";

auto deviceTypeSet() -> QSet<QByteArray> {
  QSet<QByteArray> types;
  const QList<QByteArray> parts = MediaBackendConfig::decodingHwDeviceTypes().split(',');
  for (const QByteArray &part : parts) {
    const QByteArray trimmed = part.trimmed();
    if (!trimmed.isEmpty()) {
      types.insert(trimmed);
    }
  }
  return types;
}
} // namespace

class TestMediaBackendConfig : public QObject {
  Q_OBJECT
private slots:
  void cleanup();
  void deviceTypes_excludeTheLeakingVulkanBackend();
  void deviceTypes_excludeEveryBackendQtDocumentsAsUntested();
  void deviceTypes_stillOfferHardwareDecodingPerPlatform();
  void apply_setsPolicyWhenUnset();
  void apply_keepsExplicitOverride();
  void apply_keepsExplicitHardwareDecodingOptOut();
  void childEnv_dropsTheValueWeInjected();
  void childEnv_keepsAnOperatorSuppliedValue();
};

void TestMediaBackendConfig::cleanup() {
  qunsetenv(kVar);
}

void TestMediaBackendConfig::deviceTypes_excludeTheLeakingVulkanBackend() {
  QVERIFY(!deviceTypeSet().contains(QByteArrayLiteral("vulkan")));
}

void TestMediaBackendConfig::deviceTypes_excludeEveryBackendQtDocumentsAsUntested() {
  // "Advanced FFmpeg Configuration", Qt Multimedia 6.11: drm, opencl, qsv,
  // vdpau and vulkan "have not been tested with Qt Multimedia by the Qt
  // maintainers, and may not operate as intended".
  const QSet<QByteArray> types = deviceTypeSet();
  for (const QByteArray &untested :
       {QByteArrayLiteral("drm"), QByteArrayLiteral("opencl"), QByteArrayLiteral("qsv"),
        QByteArrayLiteral("vdpau"), QByteArrayLiteral("vulkan")}) {
    QVERIFY2(!types.contains(untested), untested.constData());
  }
}

void TestMediaBackendConfig::deviceTypes_stillOfferHardwareDecodingPerPlatform() {
  // The fix must not silently degrade every host to software decoding, so
  // each supported platform keeps at least one usable backend on offer.
  const QSet<QByteArray> types = deviceTypeSet();
  QVERIFY(types.contains(QByteArrayLiteral("vaapi")));        // Linux
  QVERIFY(types.contains(QByteArrayLiteral("cuda")));         // Linux/Windows NVIDIA
  QVERIFY(types.contains(QByteArrayLiteral("d3d11va")));      // Windows
  QVERIFY(types.contains(QByteArrayLiteral("videotoolbox"))); // macOS
}

void TestMediaBackendConfig::apply_setsPolicyWhenUnset() {
  qunsetenv(kVar);
  QVERIFY(MediaBackendConfig::applyDecodingHwDeviceTypePolicy());
  QCOMPARE(qgetenv(kVar), MediaBackendConfig::decodingHwDeviceTypes());
}

void TestMediaBackendConfig::apply_keepsExplicitOverride() {
  qputenv(kVar, "vaapi");
  QVERIFY(!MediaBackendConfig::applyDecodingHwDeviceTypePolicy());
  QCOMPARE(qgetenv(kVar), QByteArrayLiteral("vaapi"));
}

void TestMediaBackendConfig::apply_keepsExplicitHardwareDecodingOptOut() {
  // `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=,` is Qt's documented spelling for
  // "no hardware decoding at all" — the escape hatch for anyone hitting a
  // driver bug in a backend that is still on our list. Overwriting it with
  // the default policy would take that escape hatch away.
  qputenv(kVar, ",");
  QVERIFY(!MediaBackendConfig::applyDecodingHwDeviceTypePolicy());
  QCOMPARE(qgetenv(kVar), QByteArrayLiteral(","));
}

void TestMediaBackendConfig::childEnv_dropsTheValueWeInjected() {
  // Kartend-fmdq5: launched processes inherit our environment, so the backend
  // list main() injects would silently constrain any child that is itself a Qt
  // app using the FFmpeg backend.
  QProcessEnvironment env;
  env.insert(QString::fromLatin1(kVar),
             QString::fromLatin1(MediaBackendConfig::decodingHwDeviceTypes()));
  env.insert(QStringLiteral("KARTEND_UNRELATED"), QStringLiteral("keep-me"));

  MediaBackendConfig::removeInjectedDecodingHwDeviceTypes(env);

  QVERIFY(!env.contains(QString::fromLatin1(kVar)));
  // Only that one key goes; the rest of the inherited environment is intact.
  QCOMPARE(env.value(QStringLiteral("KARTEND_UNRELATED")), QStringLiteral("keep-me"));
}

void TestMediaBackendConfig::childEnv_keepsAnOperatorSuppliedValue() {
  // A value that isn't ours came from the operator: children inherited it
  // before this scrubbing existed and should keep inheriting it, since removing
  // it would silently override a deliberate choice. Both a narrowed list and
  // the documented "no hardware decoding" spelling must survive.
  for (const QString &supplied : {QStringLiteral("vaapi"), QStringLiteral(",")}) {
    QProcessEnvironment env;
    env.insert(QString::fromLatin1(kVar), supplied);

    MediaBackendConfig::removeInjectedDecodingHwDeviceTypes(env);

    QCOMPARE(env.value(QString::fromLatin1(kVar)), supplied);
  }
}

QTEST_MAIN(TestMediaBackendConfig)
#include "test_mediabackendconfig.moc"
