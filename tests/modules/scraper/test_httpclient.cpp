// Tests for Scraper::HttpClient — specifically the response-size cap.
// Kartend-zy10: a hostile or buggy upstream could previously stream
// gigabytes into RAM because every reply ran readAll() unconditionally.
// This test stands up a local QTcpServer that writes a minimal HTTP
// response header and then keeps writing bytes until the client
// disconnects, and asserts the client aborts the reply once received
// bytes cross the configured cap.
#include <optional>

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include "errorutils.h"
#include "httpclient.h"

using ErrorUtils::ErrorCode;

namespace {

// Minimal HTTP/1.1 server that flood-streams an arbitrary payload after
// receiving any request. The constructor takes the per-chunk size and
// the total number of bytes it will *try* to write — the real test
// case stops far short of that total because the client aborts.
class FloodingServer : public QObject {
  Q_OBJECT
public:
  explicit FloodingServer(qint64 totalBytes, int chunkSize = 1024, QObject *parent = nullptr)
      : QObject(parent), m_totalBytes(totalBytes), m_chunkSize(chunkSize) {
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &FloodingServer::handleConnection);
  }

  bool start() { return m_server->listen(QHostAddress::LocalHost, 0); }

  quint16 port() const { return m_server->serverPort(); }

private slots:
  void handleConnection() {
    QTcpSocket *sock = m_server->nextPendingConnection();
    if (!sock) return;

    auto state = std::make_shared<ConnectionState>();
    state->bytesWritten = 0;

    // Wait for the request line + headers terminator, then respond. We
    // don't parse the request — any GET produces the same flood.
    connect(sock, &QTcpSocket::readyRead, this, [this, sock, state]() {
      state->buffer += sock->readAll();
      if (!state->headersSent && state->buffer.contains("\r\n\r\n")) {
        state->headersSent = true;
        // HTTP/1.1 with no Content-Length and Connection: close — the
        // body length is bounded only by when the server closes.
        const QByteArray headers = "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: application/octet-stream\r\n"
                                   "Connection: close\r\n"
                                   "\r\n";
        sock->write(headers);
        writeChunks(sock, state);
      }
    });

    // The client's abort() closes the socket — clean up our state.
    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
  }

private:
  struct ConnectionState {
    QByteArray buffer;
    bool headersSent = false;
    qint64 bytesWritten = 0;
  };

  void writeChunks(QTcpSocket *sock, std::shared_ptr<ConnectionState> state) {
    // Pace writes with a small inter-chunk delay so QNetworkReply emits
    // downloadProgress incrementally — on a fast localhost connection
    // an unpaced flood would arrive in a single kernel/Qt buffer-fill,
    // QNAM would emit one downloadProgress at the very end (with the
    // full size), and our abort() would land after the reply has
    // already finished successfully. 5ms is well below any realistic
    // network RTT but enough for the client's event loop to drain
    // QNAM's read buffer between chunks.
    QTimer::singleShot(5, sock, [this, sock, state]() {
      if (sock->state() != QAbstractSocket::ConnectedState) return;
      const qint64 remaining = m_totalBytes - state->bytesWritten;
      if (remaining <= 0) {
        sock->disconnectFromHost();
        return;
      }
      const qint64 thisChunk = std::min<qint64>(m_chunkSize, remaining);
      QByteArray chunk(static_cast<int>(thisChunk), 'A');
      sock->write(chunk);
      sock->flush();
      state->bytesWritten += thisChunk;
      writeChunks(sock, state);
    });
  }

  QTcpServer *m_server = nullptr;
  qint64 m_totalBytes;
  int m_chunkSize;
};

} // namespace

class TestHttpClient : public QObject {
  Q_OBJECT
private slots:
  void responseExceedingCap_returnsResponseTooLargeError();
  void responseUnderCap_returnsBodySuccessfully();
};

void TestHttpClient::responseExceedingCap_returnsResponseTooLargeError() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("HttpClient::get spins up QNetworkAccessManager's internal QThread on first call, "
        "and the QNAM thread start-up window mallocs/frees Qt-internal heap buffers (QByteArray "
        "/ QArrayData) on the worker while the main thread is still inside drainHost. Qt's "
        "QThread::start / event-queue mutex synchronisation isn't observable through the "
        "stripped libQt6Core frames, so TSan flags it as a heap race on each new test process. "
        "Pre-existing — same pattern tests/suppressions/tsan.txt already documents for other Qt "
        "queued-connection arg flows. Re-enable once tests/suppressions/tsan.txt grows a "
        "called_from_lib:libQt6Core-scoped pattern that can match the stripped frames.");
#endif
  // Cap at 4 KiB. Server will *try* to stream 1 MiB; we expect the
  // abort to fire well before that and the callback to surface
  // ResponseTooLarge.
  constexpr qint64 kCap = 4 * 1024;
  constexpr qint64 kServerTotal = 1024 * 1024;

  FloodingServer server(kServerTotal, /*chunkSize=*/1024);
  QVERIFY(server.start());

  QUrl url;
  url.setScheme("http");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/flood");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;

  Scraper::HttpClient::instance()->get(
      url, QStringLiteral("test-agent"),
      [&](ErrorUtils::Result<QByteArray> response) {
        received = std::move(response);
        loop.quit();
      },
      kCap);

  // Hard upper bound: if the abort never fires, the test should still
  // terminate so the suite doesn't hang forever.
  QTimer::singleShot(15000, &loop, &QEventLoop::quit);
  loop.exec();

  QVERIFY2(received.has_value(), "HttpClient callback never fired");
  QVERIFY2(received->isError(), "Expected an error result");
  QCOMPARE(received->error().code, ErrorCode::ResponseTooLarge);
}

void TestHttpClient::responseUnderCap_returnsBodySuccessfully() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("Same QNetworkAccessManager start-up TSan race as "
        "responseExceedingCap_returnsResponseTooLargeError above — both tests trip the lazy "
        "QNAM-thread init in HttpClient::drainHost.");
#endif
  // Server writes exactly 512 bytes (well under the 64 KiB cap), then
  // closes — the callback should fire with the full body.
  constexpr qint64 kCap = 64 * 1024;
  constexpr qint64 kServerTotal = 512;

  FloodingServer server(kServerTotal, /*chunkSize=*/256);
  QVERIFY(server.start());

  QUrl url;
  url.setScheme("http");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/small");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;

  Scraper::HttpClient::instance()->get(
      url, QStringLiteral("test-agent"),
      [&](ErrorUtils::Result<QByteArray> response) {
        received = std::move(response);
        loop.quit();
      },
      kCap);

  QTimer::singleShot(15000, &loop, &QEventLoop::quit);
  loop.exec();

  QVERIFY2(received.has_value(), "HttpClient callback never fired");
  QVERIFY2(!received->isError(), "Expected a successful result");
  QCOMPARE(received->value().size(), int(kServerTotal));
}

QTEST_MAIN(TestHttpClient)
#include "test_httpclient.moc"
