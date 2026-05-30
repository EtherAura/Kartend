// Tests for Scraper::HttpClient.
//
// Kartend-zy10 (response-size cap): a hostile or buggy upstream could
// previously stream gigabytes into RAM because every reply ran readAll()
// unconditionally. The cap tests stand up a local server that floods bytes
// until the client disconnects and assert the client aborts the reply once
// received bytes cross the configured cap.
//
// Kartend-0gp7 (credential placement): credential headers must ride in the
// request header block, never in the request target.
//
// Kartend-pugp.1 (HTTPS-only SSRF guard): HttpClient::get is the single choke
// point for every scraper request and some request URLs are response-derived
// (ScreenScraper medias[].url), so it now refuses any non-https scheme.
// Because of that guard the loopback servers below must speak TLS — they use
// QSslServer with a throwaway self-signed localhost cert (kTestCertPem); the
// client disables peer verification in initTestCase so the self-signed cert is
// accepted without a real CA.
#include <optional>

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include "errorutils.h"
#include "httpclient.h"

using ErrorUtils::ErrorCode;

namespace {

// Throwaway self-signed cert + RSA key, used only to secure the in-process
// 127.0.0.1 loopback test servers below. This is NOT a secret: it secures
// nothing but a localhost socket inside this single test binary, and the
// client side disables peer verification (see TestHttpClient::initTestCase).
// SAN is IP:127.0.0.1 and validity runs to 2126. Regenerate with:
//   openssl req -x509 -newkey rsa:2048 -nodes -days 36500 \
//     -subj "/CN=127.0.0.1" -addext "subjectAltName=IP:127.0.0.1" \
//     -keyout k8.pem -out cert.pem
//   openssl rsa -in k8.pem -traditional -out key.pem
const char *const kTestCertPem = R"PEM(-----BEGIN CERTIFICATE-----
MIIDHDCCAgSgAwIBAgIUYNdTWkfMI8DAZ/UdE5PBNVEl1lAwDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJMTI3LjAuMC4xMCAXDTI2MDUzMDIyNTQ1OVoYDzIxMjYw
NTA2MjI1NDU5WjAUMRIwEAYDVQQDDAkxMjcuMC4wLjEwggEiMA0GCSqGSIb3DQEB
AQUAA4IBDwAwggEKAoIBAQCXzrlnzdGRfHlXJh2R7JPaPXcLJj2bggjZ4iEJvuvw
RvHrMOvvmztM6h0gfDsLL0rZBbBOqW1fTXcl/4j1ZwMsZE05zoudusfTjyRHjV63
eHO1G2Lpa7NG7g+Fyc7DF/vp07gI1LtXtP/2LKOJpc67QY5tvHOWkiOgXoJkKHoF
9gVu72HvWkN1a0tFdT4ycYBjvwrTejaAO3eOQstBUhukDvHEf5bS6kj7abB/mPJl
v1g9evG2HEUS2uBQzucSpHWkoX8uJGd4H5VAt5qN0LISpkR5FP7HvzQczk4H6DMl
UWiQ1vYJggVmsBqF6HfNla2JonmyOYHrSPUWJZlJGNa3AgMBAAGjZDBiMB0GA1Ud
DgQWBBRvPhuqsFJ5trATw5881OpgR9p3UjAfBgNVHSMEGDAWgBRvPhuqsFJ5trAT
w5881OpgR9p3UjAPBgNVHRMBAf8EBTADAQH/MA8GA1UdEQQIMAaHBH8AAAEwDQYJ
KoZIhvcNAQELBQADggEBAI1Mq+fVJurCq69IFPTKyC1l8mwdmrBLHIxMKn64xbNT
WcW6gsUPo7I2jrq3NtNGD4maq/Q6sMxFmJTa9IeIeM/8cf8qyCWgs2TJP3gMz5cf
SBJs51PLK2q7mEEeBDbhTdpgYBUVdFkkz0Q3Jk8T1oAFrhQeXSiTWnf+hzxnDN2V
oCwuMtp4Jrzq+XXfvVvV4U5kLBdPaP7OnohHDsBknHDdKinVcSY/1gvWDqNP3kd2
CXv3fQ52M8KqTg99F5+Zlex7vsj0nfGeiZNLbXGJEG8kzs11c/grbsuHuh+c3xMN
srGUIvrVAxpGRazbqpuWFu1pZenmtNoxmM/yARxnHc0=
-----END CERTIFICATE-----
)PEM";

const char *const kTestKeyPem = R"PEM(-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAl865Z83RkXx5VyYdkeyT2j13CyY9m4II2eIhCb7r8Ebx6zDr
75s7TOodIHw7Cy9K2QWwTqltX013Jf+I9WcDLGRNOc6LnbrH048kR41et3hztRti
6WuzRu4PhcnOwxf76dO4CNS7V7T/9iyjiaXOu0GObbxzlpIjoF6CZCh6BfYFbu9h
71pDdWtLRXU+MnGAY78K03o2gDt3jkLLQVIbpA7xxH+W0upI+2mwf5jyZb9YPXrx
thxFEtrgUM7nEqR1pKF/LiRneB+VQLeajdCyEqZEeRT+x780HM5OB+gzJVFokNb2
CYIFZrAaheh3zZWtiaJ5sjmB60j1FiWZSRjWtwIDAQABAoIBACvWUWntYGAfzrZg
1lcmNwflifPZRh8a7M1mZF35GQ7YndFp3ifh7rzmOiUAWth+/qEu6Fu+x0unBgoe
AYHEDoGKMVbJEz4oCr5H7pUO+NQIX3lkACsho7KO2kKrJR7nVSKPtewu6i6IoQWI
nG0KSWl/o86ChepsJwePYx3jJmGD2Ns+yqSXba813qlfLeS0peWg9TUkP9f4Q6bq
xYCyWjwYViwVoxDJAYGq9QIxDT+DkLekUbXgZnd0yzSfwtLAOozEY7HvzWoWyqMg
Kfh7hFq7ruIiOiFABCPe+ubU0Ih6sY7y7jOnzRNT+uwwNwhaIU70EHxBHk0koPCr
X1BKyNECgYEAzOJgdB1ugrx1cIo6Qk/w9RfsN1jm3YzrKhzTYhIowqi1OHe6Tugl
RwB/z4p7Uv1CcICd4B8wsXKzYr1hzskJ/uIDokFp9PN7ZXBvbL3Qcj+sghRsHWMP
HYgZKFIAfjWuD4vjUxGHVIStl4yxXy/DA/Htd5X1AmzdrsFtDDqQjW8CgYEAva5r
3kklyzX70lPpp9DGOjz5B14KcKkTWsdCQHYc7j9DIUbgGmSKFG9706iJwJGjyt4/
db3LqWEMAhFngIwdhhKrtV373Ata0dCmXeEGZI9vNOcEx6UTJEkMjIFiHzbh1Cy6
bbRiaB5ys6swbKAiQqn2rSEFXBAX1qykh/mhtzkCgYEAnc5nGEhrDAt4MTxmbxj/
sOfCK0cwWsjlgMQ/FDSEbJphKqMdPxWTUMLTrtks79jdyaVm9G9Ro/uCq7TOluVF
66nNvrW/lMnM627Ug98XpEfi6TYtp9zakZZ4OhQfCRbzgEnwx9SidbjTs/zLyVMS
VAGNNCSuWDXd8XJOObMKD8UCgYB1JTLTXsOrpBR5Sn/Et8ilESEPrsGt4I3mg6dk
Hk4xyfpQo/Al/K/WfR+xkaY5uvi4gtgYhHYyjpAW+t68YkydkAxh/8BbntuhN0Z4
NlB3bKpWttKZ5lZTE5ZfdEzAUGnaWyFsPXqFKUDXu8M1YxSlrUh+liU0PXArkgYv
QDni6QKBgFJ81vaQm0T6Dt65ZDg6xw1BBUqJSFm+TJNG1ympilZhn9sHf5XMg9Vj
q8ZbSyHM+BzEL5d1nPzxyK/gqkbQpYuyeHvIfZedABmtA/2opUCtfkSxjNmEqekh
yUZeFNbjiLlZoN4ZHVgCc8eOLiEo6o3G9PEQM2Upjn3oGLBsKSny
-----END RSA PRIVATE KEY-----
)PEM";

// Server-side TLS config: present the throwaway cert/key and pin ALPN to
// HTTP/1.1 so the hand-rolled HTTP/1.1 responses below are never preempted by
// an h2 upgrade (HttpClient sets Http2AllowedAttribute on every request, so
// QNetworkAccessManager offers h2 in the ClientHello).
QSslConfiguration serverTlsConfig() {
  QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
  cfg.setLocalCertificate(QSslCertificate(QByteArray(kTestCertPem), QSsl::Pem));
  cfg.setPrivateKey(QSslKey(QByteArray(kTestKeyPem), QSsl::Rsa, QSsl::Pem));
  cfg.setAllowedNextProtocols({QByteArrayLiteral("http/1.1")});
  return cfg;
}

// Minimal HTTPS/1.1 server that flood-streams an arbitrary payload after
// receiving any request. The constructor takes the per-chunk size and the
// total number of bytes it will *try* to write — the real test case stops far
// short of that total because the client aborts. QSslServer hands back an
// already-encrypted QSslSocket from nextPendingConnection().
class FloodingServer : public QObject {
  Q_OBJECT
public:
  explicit FloodingServer(qint64 totalBytes, int chunkSize = 1024, QObject *parent = nullptr)
      : QObject(parent), m_totalBytes(totalBytes), m_chunkSize(chunkSize) {
    m_server = new QSslServer(this);
    m_server->setSslConfiguration(serverTlsConfig());
    // QSslServer queues a connection (and emits pendingConnectionAvailable)
    // only once the TLS handshake is encrypted — it does NOT emit the plain
    // QTcpServer::newConnection. nextPendingConnection() then returns the
    // already-encrypted QSslSocket.
    connect(m_server, &QTcpServer::pendingConnectionAvailable, this,
            &FloodingServer::handleConnection);
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

  QSslServer *m_server = nullptr;
  qint64 m_totalBytes;
  int m_chunkSize;
};

// Minimal HTTPS/1.1 server that records the request head (request line +
// headers, up to the blank-line terminator) and replies with a tiny fixed
// body. Used to assert what HttpClient actually puts on the wire —
// specifically that credential headers ride in the header block and not in the
// request target.
class CapturingServer : public QObject {
  Q_OBJECT
public:
  explicit CapturingServer(QObject *parent = nullptr) : QObject(parent) {
    m_server = new QSslServer(this);
    m_server->setSslConfiguration(serverTlsConfig());
    // See FloodingServer: QSslServer signals readiness via
    // pendingConnectionAvailable, not newConnection.
    connect(m_server, &QTcpServer::pendingConnectionAvailable, this,
            &CapturingServer::handleConnection);
  }

  bool start() { return m_server->listen(QHostAddress::LocalHost, 0); }
  quint16 port() const { return m_server->serverPort(); }
  // Captured request head (everything before the CRLFCRLF terminator).
  QByteArray requestHead() const { return m_requestHead; }

private slots:
  void handleConnection() {
    QTcpSocket *sock = m_server->nextPendingConnection();
    if (!sock) return;

    auto buffer = std::make_shared<QByteArray>();
    connect(sock, &QTcpSocket::readyRead, this, [this, sock, buffer]() {
      *buffer += sock->readAll();
      const int term = buffer->indexOf("\r\n\r\n");
      if (term < 0 || !m_requestHead.isEmpty()) return; // wait for full head / already served
      m_requestHead = buffer->left(term);
      const QByteArray body = "{}";
      const QByteArray resp = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: " +
                              QByteArray::number(body.size()) +
                              "\r\n"
                              "Connection: close\r\n\r\n" +
                              body;
      sock->write(resp);
      sock->flush();
      sock->disconnectFromHost();
    });
    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
  }

private:
  QSslServer *m_server = nullptr;
  QByteArray m_requestHead;
};

} // namespace

class TestHttpClient : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void nonHttpsUrl_isRefusedSynchronously();
  void responseExceedingCap_returnsResponseTooLargeError();
  void responseUnderCap_returnsBodySuccessfully();
  void requestHeaders_rideInHeaderBlockNotUrl();
};

void TestHttpClient::initTestCase() {
  // The loopback servers present a self-signed localhost cert. QtNetwork uses
  // the process-wide default QSslConfiguration for outgoing https requests
  // (HttpClient sets no per-request config), so disable peer verification here
  // to accept that cert without standing up a real CA. Scoped to this test
  // binary's process — QTEST_MAIN gives each test file its own executable.
  QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
  cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
  QSslConfiguration::setDefaultConfiguration(cfg);
}

// Kartend-pugp.1: response-derived URLs (e.g. ScreenScraper medias[].url) flow
// untrusted into HttpClient::get. Any non-https scheme — file:// (local read),
// http:// (plaintext + internal/link-local SSRF), qrc:// / data:// / ftp:// —
// must be refused at the choke point. The guard rejects before any network
// work, so the callback fires synchronously and QNAM's worker thread never
// starts (hence, unlike the network tests below, no ThreadSanitizer skip).
void TestHttpClient::nonHttpsUrl_isRefusedSynchronously() {
  const QStringList rejected = {
      QStringLiteral("http://127.0.0.1/x"), // plaintext + loopback/internal host
      QStringLiteral("http://169.254.169.254/latest/meta-data"), // link-local SSRF
      QStringLiteral("file:///etc/passwd"),                      // local file read
      QStringLiteral("ftp://example.com/x"),                     // non-web transport
      QStringLiteral("qrc:/embedded"),                           // Qt resource scheme
      QStringLiteral("data:text/plain,hello"),                   // inline data scheme
  };

  for (const QString &spec : rejected) {
    std::optional<ErrorUtils::Result<QByteArray>> received;
    bool firedSynchronously = false;
    Scraper::HttpClient::instance()->get(QUrl(spec), {{"User-Agent", "test-agent"}},
                                         [&](ErrorUtils::Result<QByteArray> response) {
                                           received = std::move(response);
                                           firedSynchronously = true;
                                         });

    QVERIFY2(firedSynchronously,
             qPrintable(QStringLiteral("callback did not fire synchronously for %1").arg(spec)));
    QVERIFY2(received.has_value(), qPrintable(QStringLiteral("no result for %1").arg(spec)));
    QVERIFY2(received->isError(), qPrintable(QStringLiteral("expected an error for %1").arg(spec)));
    QCOMPARE(received->error().code, ErrorCode::InvalidArgument);
  }
}

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
  url.setScheme("https");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/flood");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;

  Scraper::HttpClient::instance()->get(
      url, {{"User-Agent", "test-agent"}},
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
  url.setScheme("https");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/small");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;

  Scraper::HttpClient::instance()->get(
      url, {{"User-Agent", "test-agent"}},
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

// Kartend-0gp7: credential headers (e.g. TMDB's Authorization: Bearer)
// must be transmitted as request headers, never folded into the request
// target where they'd leak via Referer / proxy access logs. This locks
// in that HttpClient::get applies the RawHeaders list verbatim and keeps
// the token out of the URL line.
void TestHttpClient::requestHeaders_rideInHeaderBlockNotUrl() {
#if defined(__SANITIZE_THREAD__) || (defined(__has_feature) && __has_feature(thread_sanitizer))
  QSKIP("Same QNetworkAccessManager start-up TSan race as the size-cap tests above — the lazy "
        "QNAM-thread init in HttpClient::drainHost trips it on each new test process.");
#endif
  CapturingServer server;
  QVERIFY(server.start());

  QUrl url;
  url.setScheme("https");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/detail");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;

  Scraper::HttpClient::instance()->get(
      url, {{"User-Agent", "kartend-test"}, {"Authorization", "Bearer secret-token-123"}},
      [&](ErrorUtils::Result<QByteArray> response) {
        received = std::move(response);
        loop.quit();
      });

  QTimer::singleShot(15000, &loop, &QEventLoop::quit);
  loop.exec();

  QVERIFY2(received.has_value(), "HttpClient callback never fired");
  QVERIFY2(!received->isError(), "Expected a successful result");

  const QByteArray head = server.requestHead();
  QVERIFY2(!head.isEmpty(), "Server captured no request head");
  // Both headers must be present, verbatim, in the request head.
  QVERIFY2(head.contains("Authorization: Bearer secret-token-123"),
           "Authorization header missing or altered on the wire");
  QVERIFY2(head.contains("User-Agent: kartend-test"), "User-Agent header missing on the wire");
  // The secret must NOT appear in the request line (GET <target> HTTP/1.1) —
  // i.e. it never regressed back into the query string.
  const QByteArray requestLine = head.left(head.indexOf("\r\n"));
  QVERIFY2(!requestLine.contains("secret-token-123"),
           "Bearer token leaked into the request target / URL");
}

QTEST_MAIN(TestHttpClient)
#include "test_httpclient.moc"
