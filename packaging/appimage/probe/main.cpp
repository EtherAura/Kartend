// kartend-appimage-probe: checks, from inside an unpacked Kartend AppImage,
// that the Qt it carries can really do what Kartend needs of it.
//
// It is not part of the AppImage. build-appimage.sh builds it against the same
// Qt, and smoke-test.sh copies it next to the unpacked kartend executable (so
// it picks up the bundle's qt.conf, libraries and plugins) and runs it there.
// Starting kartend proves the platform plugin and the database; this proves
// the parts a plain start never touches:
//
//   - SQLite: a database is created, written and read back;
//   - TLS: Qt finds a working OpenSSL (the bundled one);
//   - image formats: the artwork formats have their plugins;
//   - FFmpeg: the media backend loads and decodes a short audio file that the
//     probe writes itself, and, for every file given with --media, plays its
//     video track into a frame sink and decodes its audio track. The two
//     clips beside this file (one second of a test pattern and a tone, as
//     H.264 + AAC in MP4 and as VP9 + Opus in WebM) are what smoke-test.sh
//     passes: the formats video previews most often come in.
//
// One line per check on stdout, "ok: ..." or "FAIL: ...". Exit status 0 when
// all passed, 1 otherwise. Needs no display (QT_QPA_PLATFORM=offscreen) and
// no network.

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QByteArray>
#include <QDataStream>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QLibraryInfo>
#include <QMediaFormat>
#include <QMediaPlayer>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>

namespace {

int g_failures = 0;

void report(bool ok, const QString &what) {
  QTextStream out(stdout);
  out << (ok ? "ok: " : "FAIL: ") << what << Qt::endl;
  if (!ok) {
    ++g_failures;
  }
}

void checkSqlite() {
  if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
    report(false, QStringLiteral("SQLite: the QSQLITE driver is not available"));
    return;
  }
  QString detail;
  bool ok = false;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("probe"));
    db.setDatabaseName(QStringLiteral(":memory:"));
    if (!db.open()) {
      detail = db.lastError().text();
    } else {
      QSqlQuery query(db);
      ok = query.exec(QStringLiteral("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)")) &&
           query.exec(QStringLiteral("INSERT INTO t (name) VALUES ('kartend')")) &&
           query.exec(QStringLiteral("SELECT name, sqlite_version() FROM t")) && query.next() &&
           query.value(0).toString() == QStringLiteral("kartend");
      detail = ok ? QStringLiteral("SQLite %1").arg(query.value(1).toString())
                  : query.lastError().text();
    }
  }
  QSqlDatabase::removeDatabase(QStringLiteral("probe"));
  report(ok, ok ? QStringLiteral("SQLite: created, wrote and read a database (%1)").arg(detail)
                : QStringLiteral("SQLite: %1").arg(detail));
}

void checkTls() {
  const bool ok = QSslSocket::supportsSsl();
  report(ok, ok ? QStringLiteral("TLS: backend '%1', %2")
                      .arg(QSslSocket::activeBackend(), QSslSocket::sslLibraryVersionString())
                : QStringLiteral("TLS: no working TLS backend (available: %1; built against %2)")
                      .arg(QSslSocket::availableBackends().join(QStringLiteral(", ")),
                           QSslSocket::sslLibraryBuildVersionString()));
}

void checkImageFormats() {
  const QList<QByteArray> have = QImageReader::supportedImageFormats();
  QStringList missing;
  for (const char *format : {"png", "jpeg", "gif", "webp", "svg", "ico", "tiff"}) {
    if (!have.contains(QByteArray(format))) {
      missing << QString::fromLatin1(format);
    }
  }
  report(
      missing.isEmpty(),
      missing.isEmpty()
          ? QStringLiteral("image formats: %1 readable, among them png jpeg gif webp svg ico tiff")
                .arg(have.size())
          : QStringLiteral("image formats: no reader for %1").arg(missing.join(QLatin1Char(' '))));
}

// A quarter of a second of a 440 Hz tone: 8 kHz, mono, 16-bit PCM in a WAV
// container. Small, but it has to go through FFmpeg's demuxer and decoder.
constexpr int kSampleRate = 8000;
constexpr int kSampleCount = 2000;

bool writeWav(const QString &path) {
  QByteArray samples;
  {
    QDataStream stream(&samples, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    constexpr double kTwoPi = 6.283185307179586;
    for (int i = 0; i < kSampleCount; ++i) {
      stream << static_cast<qint16>(12000.0 * std::sin(kTwoPi * 440.0 * i / kSampleRate));
    }
  }
  QByteArray wav;
  {
    QDataStream stream(&wav, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << static_cast<quint32>(36 + samples.size());
    stream.writeRawData("WAVEfmt ", 8);
    stream << static_cast<quint32>(16)              // fmt chunk size
           << static_cast<quint16>(1)               // PCM
           << static_cast<quint16>(1)               // channels
           << static_cast<quint32>(kSampleRate)     // sample rate
           << static_cast<quint32>(kSampleRate * 2) // bytes per second
           << static_cast<quint16>(2)               // block align
           << static_cast<quint16>(16);             // bits per sample
    stream.writeRawData("data", 4);
    stream << static_cast<quint32>(samples.size());
  }
  wav.append(samples);
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(wav) == wav.size();
}

// decodeAudio: run PATH's audio track through QAudioDecoder. Returns the
// number of decoded frames; ERROR is set when the decoder reported one or
// never finished.
qint64 decodeAudio(const QString &path, QString *error) {
  QAudioDecoder decoder;
  QEventLoop loop;
  qint64 frames = 0;
  QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&]() {
    const QAudioBuffer buffer = decoder.read();
    frames += buffer.frameCount();
  });
  QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
  QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop,
                   [&](QAudioDecoder::Error) {
                     *error = decoder.errorString();
                     loop.quit();
                   });
  // Never hang a CI job on a decoder that neither finishes nor fails.
  QTimer::singleShot(30000, &loop, [&]() {
    *error = QStringLiteral("no result after 30 s");
    loop.quit();
  });
  decoder.setSource(QUrl::fromLocalFile(path));
  decoder.start();
  loop.exec();
  return frames;
}

void checkGeneratedAudio() {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("tone.wav"));
  if (!dir.isValid() || !writeWav(path)) {
    report(false, QStringLiteral("media decode: could not write the test file"));
    return;
  }
  QString error;
  const qint64 frames = decodeAudio(path, &error);
  // A resampler may round the frame count; a decoder that works returns
  // nearly all of the 2000 frames, a broken one returns none.
  const bool ok = error.isEmpty() && frames >= kSampleCount * 9 / 10;
  report(ok, ok ? QStringLiteral("media decode: %1 audio frames from a %2-frame WAV file")
                      .arg(frames)
                      .arg(kSampleCount)
                : QStringLiteral("media decode: WAV: %1 (%2 frames decoded)")
                      .arg(error.isEmpty() ? QStringLiteral("too few frames") : error)
                      .arg(frames));
}

// Play PATH's video track into a sink (no window, no audio device) and count
// the frames that arrive. This is the path Kartend's video previews take:
// demuxer, video decoder and frame conversion, on whatever this machine has,
// which in a container means software decoding.
void checkMediaFile(const QString &path) {
  const QString name = QFileInfo(path).fileName();
  if (!QFileInfo::exists(path)) {
    report(false, QStringLiteral("media file %1: not found").arg(path));
    return;
  }

  QMediaPlayer player;
  QVideoSink sink;
  QEventLoop loop;
  int frames = 0;
  QSize size;
  QString error;
  QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop, [&](const QVideoFrame &frame) {
    if (frame.isValid()) {
      ++frames;
      size = frame.size();
    }
  });
  QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop,
                   [&](QMediaPlayer::Error, const QString &text) {
                     error = text;
                     loop.quit();
                   });
  QObject::connect(
      &player, &QMediaPlayer::mediaStatusChanged, &loop, [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia) {
          loop.quit();
        }
      });
  QTimer::singleShot(30000, &loop, [&]() {
    error = QStringLiteral("no end of playback after 30 s");
    loop.quit();
  });
  player.setVideoSink(&sink);
  player.setSource(QUrl::fromLocalFile(path));
  player.play();
  loop.exec();
  player.stop();

  const bool videoOk = error.isEmpty() && frames > 0 && !size.isEmpty();
  report(videoOk, videoOk
                      ? QStringLiteral("video %1: played, %2 frames of %3x%4 reached the sink")
                            .arg(name)
                            .arg(frames)
                            .arg(size.width())
                            .arg(size.height())
                      : QStringLiteral("video %1: %2 (%3 frames)")
                            .arg(name, error.isEmpty() ? QStringLiteral("no frame decoded") : error)
                            .arg(frames));

  QString audioError;
  const qint64 audioFrames = decodeAudio(path, &audioError);
  const bool audioOk = audioError.isEmpty() && audioFrames > 0;
  report(audioOk, audioOk ? QStringLiteral("audio %1: %2 frames decoded").arg(name).arg(audioFrames)
                          : QStringLiteral("audio %1: %2 (%3 frames)")
                                .arg(name, audioError.isEmpty() ? QStringLiteral("nothing decoded")
                                                                : audioError)
                                .arg(audioFrames));
}

// What Qt reports as supported. Printed for the record, not judged: for
// decoding Qt lists the codecs it can also ENCODE (plus WMV), so on a
// machine without a hardware encoder H.264, H.265, VP8 and VP9 are absent
// from this list although they decode fine. Playing the files above is the
// real test.
void printReportedCodecs() {
  QMediaFormat formats;
  QStringList names;
  for (const auto codec : formats.supportedVideoCodecs(QMediaFormat::Decode)) {
    names << QMediaFormat::videoCodecName(codec);
  }
  names.sort();
  QTextStream(stdout) << "note: Qt lists as video codecs (those it can also encode here): "
                      << names.join(QStringLiteral(", ")) << Qt::endl;
}

} // namespace

int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);

  // --media FILE, any number of times.
  QStringList mediaFiles;
  const QStringList arguments = QCoreApplication::arguments();
  for (qsizetype i = 1; i < arguments.size(); ++i) {
    if (arguments.at(i) == QStringLiteral("--media") && i + 1 < arguments.size()) {
      mediaFiles << arguments.at(++i);
    } else {
      QTextStream(stderr) << "usage: kartend-appimage-probe [--media FILE]..." << Qt::endl;
      return 2;
    }
  }

  QTextStream(stdout) << "probe: Qt " << qVersion() << ", plugins in "
                      << QLibraryInfo::path(QLibraryInfo::PluginsPath) << Qt::endl;
  checkSqlite();
  checkTls();
  checkImageFormats();
  checkGeneratedAudio();
  for (const QString &file : mediaFiles) {
    checkMediaFile(file);
  }
  printReportedCodecs();

  QTextStream(stdout) << (g_failures == 0 ? "probe: all checks passed" : "probe: FAILED")
                      << Qt::endl;
  return g_failures == 0 ? 0 : 1;
}
