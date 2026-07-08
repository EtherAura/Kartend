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
#include <thread>

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

// macOS Qt defaults to the Secure Transport TLS backend, which has no usable
// server-side support — the in-process QSslServer the cases below stand up
// never completes a handshake, so every request errors (Kartend-0a5p4). The
// SSRF / allowlist / redirect / size-cap logic is platform-independent and
// covered on Linux (Qt 6.4 + 6.8) and Windows; production macOS uses Secure
// Transport as a client, which works. Skip just the local-TLS-server cases.
#if defined(Q_OS_MACOS)
#define SKIP_LOCAL_TLS_SERVER_ON_MACOS()                                                           \
  QSKIP("local QSslServer has no server-side TLS on macOS Secure Transport (Kartend-0a5p4)")
#else
#define SKIP_LOCAL_TLS_SERVER_ON_MACOS() ((void)0)
#endif

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

// Minimal HTTPS/1.1 server for the redirect-guard tests (Kartend-pugp.2). The
// first request it sees is answered with a 302 to setLocation(); any later
// request (i.e. a redirect the client was allowed to follow back to this same
// host) is answered 200 with kRedirectBody. That lets one server cover both
// "redirect blocked before contact" (only the 302 is ever served) and
// "same-host redirect followed end to end" (302 then 200).
class RedirectingServer : public QObject {
  Q_OBJECT
public:
  static constexpr const char *kRedirectBody = "REDIRECTED-OK";

  explicit RedirectingServer(QObject *parent = nullptr) : QObject(parent) {
    m_server = new QSslServer(this);
    m_server->setSslConfiguration(serverTlsConfig());
    connect(m_server, &QTcpServer::pendingConnectionAvailable, this,
            &RedirectingServer::handleConnection);
  }

  bool start() { return m_server->listen(QHostAddress::LocalHost, 0); }
  quint16 port() const { return m_server->serverPort(); }
  // Absolute URL sent in the first response's Location header. Set after
  // start() when the target is this same server (the port is only known then).
  void setLocation(const QByteArray &location) { m_location = location; }

private slots:
  void handleConnection() {
    QTcpSocket *sock = m_server->nextPendingConnection();
    if (!sock) return;

    auto buffer = std::make_shared<QByteArray>();
    auto responded = std::make_shared<bool>(false);
    connect(sock, &QTcpSocket::readyRead, this, [this, sock, buffer, responded]() {
      if (*responded) return;
      *buffer += sock->readAll();
      if (!buffer->contains("\r\n\r\n")) return;
      *responded = true;

      QByteArray resp;
      if (!m_servedRedirect) {
        m_servedRedirect = true;
        resp = "HTTP/1.1 302 Found\r\n"
               "Location: " +
               m_location +
               "\r\n"
               "Content-Length: 0\r\n"
               "Connection: close\r\n\r\n";
      } else {
        const QByteArray body = QByteArray(kRedirectBody);
        resp = "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/plain\r\n"
               "Content-Length: " +
               QByteArray::number(body.size()) +
               "\r\n"
               "Connection: close\r\n\r\n" +
               body;
      }
      sock->write(resp);
      sock->flush();
      sock->disconnectFromHost();
    });
    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
  }

private:
  QSslServer *m_server = nullptr;
  QByteArray m_location;
  bool m_servedRedirect = false;
};

// Minimal HTTPS/1.1 server that answers every request with one canned raw
// response. Used by the error-body sanitization test to serve an HTTP error
// whose body carries CR/LF and terminal-escape bytes.
class StaticResponseServer : public QObject {
  Q_OBJECT
public:
  explicit StaticResponseServer(QByteArray response, QObject *parent = nullptr)
      : QObject(parent), m_response(std::move(response)) {
    m_server = new QSslServer(this);
    m_server->setSslConfiguration(serverTlsConfig());
    // See FloodingServer: QSslServer signals readiness via
    // pendingConnectionAvailable, not newConnection.
    connect(m_server, &QTcpServer::pendingConnectionAvailable, this,
            &StaticResponseServer::handleConnection);
  }

  bool start() { return m_server->listen(QHostAddress::LocalHost, 0); }
  quint16 port() const { return m_server->serverPort(); }

private slots:
  void handleConnection() {
    QTcpSocket *sock = m_server->nextPendingConnection();
    if (!sock) return;
    auto buffer = std::make_shared<QByteArray>();
    connect(sock, &QTcpSocket::readyRead, this, [this, sock, buffer]() {
      *buffer += sock->readAll();
      if (!buffer->contains("\r\n\r\n")) return;
      sock->write(m_response);
      sock->flush();
      sock->disconnectFromHost();
    });
    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
  }

private:
  QSslServer *m_server = nullptr;
  QByteArray m_response;
};

} // namespace

class TestHttpClient : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void nonHttpsUrl_isRefusedSynchronously();
  void hostOutsideAllowlist_isRefusedSynchronously();
  void allowlistedHost_servesBodyWithoutRedirect();
  void redirectOutsideAllowlist_isRefused();
  void redirectToAllowlistedHost_isFollowed();
  void responseExceedingCap_returnsResponseTooLargeError();
  void responseUnderCap_returnsBodySuccessfully();
  void requestHeaders_rideInHeaderBlockNotUrl();
  void clearPending_cancelsQueuedRequestsViaCallback();
  void errorBody_controlCharacters_areFlattenedInDetails();
  void errorDetails_redactCredentialQueryValues();
  void unconfiguredHost_defaultsToOneInFlightRequest();
  void get_fromWrongThread_isRefusedInReleaseBuilds();
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

// Kartend-pugp.2: when a caller pins an allowlist (ScreenScraper media fetches
// pass {"screenscraper.fr"}), an initial host outside it must be refused at the
// choke point with no network work — same synchronous path as the https guard.
// The look-alike entries are the ones that matter: a naive endsWith() check
// would wrongly admit "evilscreenscraper.fr" (no dot boundary) and treating the
// domain as a left-label ("screenscraper.fr.attacker.test") is the classic
// suffix-match bypass.
void TestHttpClient::hostOutsideAllowlist_isRefusedSynchronously() {
  const QStringList allow = {QStringLiteral("screenscraper.fr")};
  const QStringList rejected = {
      QStringLiteral("https://evil.example/x"),                   // unrelated host
      QStringLiteral("https://127.0.0.1/x"),                      // internal/loopback host
      QStringLiteral("https://169.254.169.254/meta-data"),        // link-local metadata SSRF
      QStringLiteral("https://evilscreenscraper.fr/x"),           // suffix look-alike, no dot
      QStringLiteral("https://screenscraper.fr.attacker.test/x"), // domain as a left label
  };

  for (const QString &spec : rejected) {
    std::optional<ErrorUtils::Result<QByteArray>> received;
    bool firedSynchronously = false;
    Scraper::HttpClient::instance()->get(
        QUrl(spec), {{"User-Agent", "test-agent"}},
        [&](ErrorUtils::Result<QByteArray> response) {
          received = std::move(response);
          firedSynchronously = true;
        },
        Scraper::HttpClient::kDefaultMaxResponseBytes, QString(), allow);

    QVERIFY2(firedSynchronously,
             qPrintable(QStringLiteral("callback did not fire synchronously for %1").arg(spec)));
    QVERIFY2(received.has_value(), qPrintable(QStringLiteral("no result for %1").arg(spec)));
    QVERIFY2(received->isError(), qPrintable(QStringLiteral("expected an error for %1").arg(spec)));
    QCOMPARE(received->error().code, ErrorCode::InvalidArgument);
  }
}

// Kartend-pugp.2: a host *inside* the allowlist passes the guard and is served
// normally. This also confirms UserVerifiedRedirectPolicy (which the allowlist
// path switches on) doesn't stall an ordinary, non-redirecting response.
void TestHttpClient::allowlistedHost_servesBodyWithoutRedirect() {
  SKIP_LOCAL_TLS_SERVER_ON_MACOS();
  constexpr qint64 kServerTotal = 256;
  FloodingServer server(kServerTotal, /*chunkSize=*/256);
  QVERIFY(server.start());

  QUrl url;
  url.setScheme("https");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/img");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;

  Scraper::HttpClient::instance()->get(url, {{"User-Agent", "test-agent"}},
                                       [&](ErrorUtils::Result<QByteArray> response) {
                                         received = std::move(response);
                                         loop.quit();
                                       },
                                       Scraper::HttpClient::kDefaultMaxResponseBytes, QString(),
                                       {QStringLiteral("127.0.0.1")});

  QTimer::singleShot(15000, &loop, &QEventLoop::quit);
  loop.exec();

  QVERIFY2(received.has_value(), "HttpClient callback never fired");
  QVERIFY2(!received->isError(), "Expected a successful result for an allowlisted host");
  QCOMPARE(received->value().size(), int(kServerTotal));
}

// Kartend-pugp.2: the core SSRF-pivot defense. The server 302-redirects to an
// off-allowlist host; the client must refuse it *before* contacting that host.
// A refused redirect surfaces as ErrorCode::InvalidArgument — distinct from the
// HostNotFoundError (NetworkRequestFailed) we'd see if the redirect were instead
// followed to the unresolved target, so this assertion proves we blocked rather
// than merely failed to connect.
void TestHttpClient::redirectOutsideAllowlist_isRefused() {
  SKIP_LOCAL_TLS_SERVER_ON_MACOS();
  RedirectingServer server;
  QVERIFY(server.start());
  server.setLocation("https://evil.example/pwned");

  QUrl url;
  url.setScheme("https");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/start");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;

  Scraper::HttpClient::instance()->get(url, {{"User-Agent", "test-agent"}},
                                       [&](ErrorUtils::Result<QByteArray> response) {
                                         received = std::move(response);
                                         loop.quit();
                                       },
                                       Scraper::HttpClient::kDefaultMaxResponseBytes, QString(),
                                       {QStringLiteral("127.0.0.1")});

  QTimer::singleShot(15000, &loop, &QEventLoop::quit);
  loop.exec();

  QVERIFY2(received.has_value(), "HttpClient callback never fired");
  QVERIFY2(received->isError(), "Expected the off-allowlist redirect to be refused");
  QCOMPARE(received->error().code, ErrorCode::InvalidArgument);
}

// Kartend-pugp.2: the allow side of redirect verification. A redirect back to a
// host *inside* the allowlist (here the same loopback server) must be followed
// to completion — this is what exercises the redirectAllowed() hand-off, so a
// regression that aborted every redirect would fail here, not silently pass.
void TestHttpClient::redirectToAllowlistedHost_isFollowed() {
  SKIP_LOCAL_TLS_SERVER_ON_MACOS();
  RedirectingServer server;
  QVERIFY(server.start());
  // Redirect back to this same server (host 127.0.0.1 is allowlisted) on a
  // different path; the second request is answered 200 with kRedirectBody.
  server.setLocation("https://127.0.0.1:" + QByteArray::number(server.port()) + "/followed");

  QUrl url;
  url.setScheme("https");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/start");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;

  Scraper::HttpClient::instance()->get(url, {{"User-Agent", "test-agent"}},
                                       [&](ErrorUtils::Result<QByteArray> response) {
                                         received = std::move(response);
                                         loop.quit();
                                       },
                                       Scraper::HttpClient::kDefaultMaxResponseBytes, QString(),
                                       {QStringLiteral("127.0.0.1")});

  QTimer::singleShot(15000, &loop, &QEventLoop::quit);
  loop.exec();

  QVERIFY2(received.has_value(), "HttpClient callback never fired");
  QVERIFY2(!received->isError(), "Expected the same-host redirect to be followed");
  QCOMPARE(received->value(), QByteArray(RedirectingServer::kRedirectBody));
}

void TestHttpClient::responseExceedingCap_returnsResponseTooLargeError() {
  SKIP_LOCAL_TLS_SERVER_ON_MACOS();
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
  SKIP_LOCAL_TLS_SERVER_ON_MACOS();
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
  SKIP_LOCAL_TLS_SERVER_ON_MACOS();
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
  // Both headers must ride in the request head. HTTP field-names are
  // case-insensitive (RFC 9110) and Qt lowercases them on the wire on some
  // versions (6.8 emits "authorization:"/"user-agent:"; 6.4/6.10 keep the
  // sent casing), so match the name case-insensitively — the value is
  // preserved verbatim. A case-sensitive "Authorization:" check here passed
  // everywhere except the Qt 6.8 CI job (Kartend-e4urr).
  const QByteArray headLower = head.toLower();
  QVERIFY2(headLower.contains("authorization: bearer secret-token-123"),
           qPrintable(QStringLiteral("Authorization header missing/altered on the wire:\n%1")
                          .arg(QString::fromLatin1(head))));
  QVERIFY2(headLower.contains("user-agent: kartend-test"),
           qPrintable(QStringLiteral("User-Agent header missing on the wire:\n%1")
                          .arg(QString::fromLatin1(head))));
  // The secret must NOT appear in the request line (GET <target> HTTP/1.1) —
  // i.e. it never regressed back into the query string.
  const QByteArray requestLine = head.left(head.indexOf("\r\n"));
  QVERIFY2(!requestLine.contains("secret-token-123"),
           "Bearer token leaked into the request target / URL");
}

// Kartend audit nujso: clearPending() must invoke each queued (not-yet-
// dispatched) request's callback with a cancelled error, not silently drop it —
// otherwise a caller waiting on the callback (e.g. a media-aggregator
// pending-count) would hang. Driven deterministically: cap a host at one
// in-flight request and fire two; with no event-loop turn in between the second
// stays queued, so clearPending must fire its callback synchronously.
void TestHttpClient::clearPending_cancelsQueuedRequestsViaCallback() {
  auto *client = Scraper::HttpClient::instance();
  client->clearPending();

  // A closed loopback port: the first request occupies the single in-flight
  // slot, then fails fast with connection-refused (drained at the end so no
  // reply outlives the test).
  const QString host = QStringLiteral("127.0.0.1");
  const QUrl url(QStringLiteral("https://127.0.0.1:1/x"));
  client->setRateLimit(host, /*intervalMs=*/0, /*maxConcurrent=*/1);

  bool inflightFired = false;
  bool queuedFired = false;
  std::optional<ErrorUtils::Result<QByteArray>> queuedResult;
  client->get(url, {{"User-Agent", "test-agent"}},
              [&](ErrorUtils::Result<QByteArray>) { inflightFired = true; });
  client->get(url, {{"User-Agent", "test-agent"}}, [&](ErrorUtils::Result<QByteArray> r) {
    queuedFired = true;
    queuedResult = std::move(r);
  });

  // No event loop has run, so the second request is still queued. Clearing it
  // must fire its callback (cancelled), not silently discard it.
  client->clearPending();
  QVERIFY(queuedFired);
  QVERIFY(queuedResult.has_value());
  QVERIFY(queuedResult->isError());
  // RequestQueueCleared, not OperationCancelled: the deliberate-cancel code is
  // excluded from RetryPolicy::isTransient so a batch cancel can't re-issue
  // the dropped lookups through the provider's retry gate (Kartend-jjyst.2).
  QCOMPARE(queuedResult->error().code, ErrorCode::RequestQueueCleared);

  // Drain the in-flight request (fast connection-refused) so no reply lingers
  // past the test, then drop the per-host rule to leave the singleton clean.
  QTRY_VERIFY_WITH_TIMEOUT(inflightFired, 10000);
  client->clearPending();
  client->setRateLimit(host, 0, 0);
}

// Log-injection hardening: a 4xx/5xx body is provider-controlled text that
// HttpClient folds into ErrorContext::details, which flows verbatim into the
// always-on kartend.errors log. Embedded CR/LF (line forging) and other
// control bytes (terminal escapes) must be flattened at the fold; only the
// single structural '\n' HttpClient itself inserts before "Response:" may
// survive, so userFacingSummary()'s first-line pick still lands on Qt's
// errorString.
void TestHttpClient::errorBody_controlCharacters_areFlattenedInDetails() {
  SKIP_LOCAL_TLS_SERVER_ON_MACOS();
  const QByteArray body = "Bad request\r\nINJECTED log line\x1b[31mred";
  const QByteArray response = "HTTP/1.1 400 Bad Request\r\n"
                              "Content-Type: text/plain\r\n"
                              "Content-Length: " +
                              QByteArray::number(body.size()) +
                              "\r\n"
                              "Connection: close\r\n\r\n" +
                              body;
  StaticResponseServer server(response);
  QVERIFY(server.start());

  QUrl url;
  url.setScheme("https");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/err");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;
  Scraper::HttpClient::instance()->get(url, {{"User-Agent", "test-agent"}},
                                       [&](ErrorUtils::Result<QByteArray> r) {
                                         received = std::move(r);
                                         loop.quit();
                                       });
  QTimer::singleShot(15000, &loop, &QEventLoop::quit);
  loop.exec();

  QVERIFY2(received.has_value(), "HttpClient callback never fired");
  QVERIFY2(received->isError(), "Expected an error result for the HTTP 400");
  const QString details = received->error().details;
  QVERIFY2(details.contains(QStringLiteral("Response:")),
           qPrintable(QStringLiteral("error body missing from details: %1").arg(details)));
  QVERIFY2(!details.contains(QLatin1Char('\r')),
           qPrintable(QStringLiteral("CR survived into details: %1").arg(details)));
  QCOMPARE(details.count(QLatin1Char('\n')), 1); // only the structural separator
  QVERIFY2(!details.contains(QChar(0x1b)),
           qPrintable(QStringLiteral("escape byte survived into details: %1").arg(details)));
  // The body content itself is preserved, just flattened onto one line.
  QVERIFY2(details.contains(QStringLiteral("Bad request INJECTED log line")),
           qPrintable(QStringLiteral("flattened body content missing: %1").arg(details)));
}

void TestHttpClient::errorDetails_redactCredentialQueryValues() {
  SKIP_LOCAL_TLS_SERVER_ON_MACOS();
  // Qt's errorString() embeds the FULL request URL ("Error transferring
  // <url> - server replied: ..."), and the server body here echoes the
  // password back the way ScreenScraper's "Erreur de login" responses can.
  // Neither copy may survive into details / userFacingSummary — they fan
  // out to the always-on error log, the failure UI, and the resume file.
  const QByteArray body = "Erreur de login: sspassword topsecret99 rejected";
  const QByteArray response = "HTTP/1.1 403 Forbidden\r\n"
                              "Content-Type: text/plain\r\n"
                              "Content-Length: " +
                              QByteArray::number(body.size()) +
                              "\r\n"
                              "Connection: close\r\n\r\n" +
                              body;
  StaticResponseServer server(response);
  QVERIFY(server.start());

  QUrl url;
  url.setScheme("https");
  url.setHost("127.0.0.1");
  url.setPort(server.port());
  url.setPath("/jeuInfos.php");
  url.setQuery("devid=cedar&devpassword=topsecret99&ssid=user1&sspassword=topsecret99&output=json");

  std::optional<ErrorUtils::Result<QByteArray>> received;
  QEventLoop loop;
  Scraper::HttpClient::instance()->get(url, {{"User-Agent", "test-agent"}},
                                       [&](ErrorUtils::Result<QByteArray> r) {
                                         received = std::move(r);
                                         loop.quit();
                                       });
  QTimer::singleShot(15000, &loop, &QEventLoop::quit);
  loop.exec();

  QVERIFY2(received.has_value(), "HttpClient callback never fired");
  QVERIFY2(received->isError(), "Expected an error result for the HTTP 403");
  const QString details = received->error().details;
  const QString summary = received->error().userFacingSummary();
  QVERIFY2(!details.contains(QStringLiteral("topsecret99")),
           qPrintable(QStringLiteral("credential value leaked into details: %1").arg(details)));
  QVERIFY2(!summary.contains(QStringLiteral("topsecret99")),
           qPrintable(QStringLiteral("credential value leaked into summary: %1").arg(summary)));
  QVERIFY2(
      details.contains(QStringLiteral("<redacted>")),
      qPrintable(
          QStringLiteral("redaction marker missing — did the URL fold move? %1").arg(details)));
}

// Locks in the documented default host policy: a host with NO setRateLimit
// rule is still capped at one in-flight request (HostPolicy{}'s defaults) —
// the conservative default the header documents. Deterministic shape borrowed
// from the clearPending test: with the single default slot occupied and no
// event-loop turn in between, the second request must still be queued, which
// clearPending proves by firing its callback. Were the default unlimited, both
// requests would dispatch immediately and there would be nothing to cancel.
void TestHttpClient::unconfiguredHost_defaultsToOneInFlightRequest() {
  auto *client = Scraper::HttpClient::instance();
  client->clearPending();

  const QString host = QStringLiteral("127.0.0.1");
  // Drop any per-host rule an earlier test registered so this host runs on
  // the implicit default policy.
  client->setRateLimit(host, 0, 0);
  // A closed loopback port: the first request occupies the default single
  // in-flight slot, then fails fast with connection-refused.
  const QUrl url(QStringLiteral("https://127.0.0.1:1/x"));

  bool inflightFired = false;
  bool queuedFired = false;
  std::optional<ErrorUtils::Result<QByteArray>> queuedResult;
  client->get(url, {{"User-Agent", "test-agent"}},
              [&](ErrorUtils::Result<QByteArray>) { inflightFired = true; });
  client->get(url, {{"User-Agent", "test-agent"}}, [&](ErrorUtils::Result<QByteArray> r) {
    queuedFired = true;
    queuedResult = std::move(r);
  });

  client->clearPending();
  QVERIFY2(queuedFired, "second request to an unconfigured host was not queued behind the first");
  QVERIFY(queuedResult.has_value());
  QVERIFY(queuedResult->isError());
  QCOMPARE(queuedResult->error().code, ErrorCode::RequestQueueCleared);

  // Drain the in-flight request (fast connection-refused) so no reply lingers
  // past the test.
  QTRY_VERIFY_WITH_TIMEOUT(inflightFired, 10000);
  client->clearPending();
}

// Kartend release-mode thread guard: HttpClient's per-host maps are
// main-thread-only. In debug builds a wrong-thread get() trips Q_ASSERT_X
// (process abort — cannot run under QTest), so this exercises the RELEASE-mode
// guard: the request must be refused via the callback (resolved on the calling
// thread) instead of silently corrupting the maps.
void TestHttpClient::get_fromWrongThread_isRefusedInReleaseBuilds() {
#ifndef QT_NO_DEBUG
  QSKIP("debug builds enforce the wrong-thread invariant with Q_ASSERT_X (which aborts); "
        "the release-mode guard runs in the Release CI legs");
#else
  auto *client = Scraper::HttpClient::instance();
  std::optional<ErrorUtils::Result<QByteArray>> received;
  bool fired = false;
  std::thread worker([&]() {
    client->get(QUrl(QStringLiteral("https://example.test/x")), {{"User-Agent", "test-agent"}},
                [&](ErrorUtils::Result<QByteArray> r) {
                  received = std::move(r);
                  fired = true;
                });
  });
  worker.join();
  QVERIFY2(fired, "wrong-thread get() must resolve its callback synchronously");
  QVERIFY(received.has_value());
  QVERIFY(received->isError());
  QCOMPARE(received->error().code, ErrorCode::InvalidArgument);
#endif
}

QTEST_MAIN(TestHttpClient)
#include "test_httpclient.moc"
