#ifndef DATHTTP_H
#define DATHTTP_H

#include <atomic>
#include <functional>
#include <memory>

#include <QByteArray>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

/// Shared blocking-request plumbing for the DAT downloaders (NoIntroDownload /
/// RedumpDownload): one request driven by a local QEventLoop with the common
/// hardening — desktop User-Agent, 30s transfer timeout, 200ms cancel-token
/// poll, and the response-size cap (Kartend-85zrx) — kept in one place so a
/// fix to any of them cannot silently miss a downloader. Only the redirect
/// policy differs per caller and is passed in.
///
/// Blocking by design: blockingRequest() drives a local QEventLoop, so the
/// caller must invoke it on a worker thread.
namespace DatHttp {

using CancelToken = std::shared_ptr<std::atomic<bool>>;

/// True when the token exists and has been tripped.
bool cancelled(const CancelToken &c);

/// Decides whether a redirect target may be followed; called before the
/// target is contacted, a rejected hop aborts the reply. Pass nullptr to
/// blockingRequest for ManualRedirectPolicy instead: redirects are not
/// followed and the 3xx reply is returned for the caller to inspect.
using RedirectAllow = std::function<bool(const QUrl &)>;

/// Issue one request and block on a local event loop until it finishes (or
/// the cancel token trips / the size cap is crossed, which abort the reply so
/// it finishes with OperationCanceledError). `post` null => GET. `referer`
/// empty => no Referer header. Progress on the reply is forwarded only when
/// onProgress is set. Returns the reply (caller owns it) so
/// headers/redirect/body are all inspectable.
QNetworkReply *blockingRequest(QNetworkAccessManager &nam, const QUrl &url, const QByteArray &post,
                               const QString &referer, const CancelToken &cancel,
                               const std::function<void(qint64, qint64)> &onProgress,
                               const RedirectAllow &allowRedirect);

} // namespace DatHttp

#endif // DATHTTP_H
