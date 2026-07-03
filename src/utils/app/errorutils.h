#ifndef ERRORUTILS_H
#define ERRORUTILS_H

#include <exception>
#include <optional>
#include <QDebug>
#include <QLoggingCategory>
#include <QString>

namespace ErrorUtils {

// Cross-cutting error logging channel. Defined inline so
// any translation unit that includes errorutils.h — including small test
// targets that don't link loggingcategories.cpp — picks up the definition.
// Always-on at info level so centralized error reports surface in release
// logs without per-module category routing.
inline const QLoggingCategory &lcErrors() {
  static const QLoggingCategory cat("kartend.errors", QtInfoMsg);
  return cat;
}

// Convert a std::exception's message to QString. Centralised so future
// formatting changes (typeid prefix, structured ErrorContext attachment,
// etc.) land in one place instead of every catch block.
[[nodiscard]] inline QString exceptionMessage(const std::exception &e) {
  return QString::fromStdString(e.what());
}

// Collapse remote / untrusted text into one bounded, log-safe line. CR/LF
// and every other ASCII control character become spaces (then whitespace
// runs collapse via simplified()) so a hostile server body folded into
// ErrorContext::details can't forge extra lines in the always-on
// kartend.errors log or smuggle terminal escape sequences; the result is
// truncated to `maxChars` so a size-capped-but-still-huge error body can't
// dominate a log entry or an error dialog. Callers apply this wherever
// remote bytes are folded into details, and logError() applies it again at
// the sink as the backstop.
[[nodiscard]] inline QString sanitizedSingleLine(QString text, int maxChars = 2048) {
  for (QChar &ch : text) {
    const char16_t u = ch.unicode();
    if (u < 0x20 || u == 0x7f) {
      ch = QLatin1Char(' ');
    }
  }
  text = text.simplified();
  if (maxChars > 0 && text.size() > maxChars) {
    text.truncate(maxChars);
    text += QStringLiteral("…");
  }
  return text;
}

} // namespace ErrorUtils

namespace ErrorUtils {

// Error severity levels for logging and handling decisions
enum class Severity {
  Info,    // Informational, not an error
  Warning, // Something unexpected but recoverable
  Error,   // An error that may affect functionality
  Critical // A critical error that prevents operation
};

// Error codes for categorizing errors across the application
enum class ErrorCode {
  // Sentinel value for non-error contexts. Test against errors via
  // ErrorContext::isError() or Result::isError() rather than comparing
  // against this enumerator directly.
  Success = 0,

  // Database errors (100-199)
  DatabaseConnectionFailed = 100,
  DatabaseQueryFailed = 101,
  DatabaseTransactionFailed = 102,
  DatabaseNotOpen = 103,
  DatabaseConnectionLost = 104,
  DatabaseConnectionRestored = 105,

  // Collection errors (200-299)
  InvalidCollectionContext = 200,
  CollectionNotFound = 201,
  MediaDirectoryNotFound = 202,
  ArtworkDirectoryNotFound = 203,

  // File errors (300-399)
  FileNotFound = 300,
  FileReadError = 301,
  FileWriteError = 302,
  InvalidFilePath = 303,

  // Configuration errors (400-499)
  ConfigLoadFailed = 400,
  ConfigSaveFailed = 401,
  InvalidConfigValue = 402,

  // UI/Widget errors (500-599)
  WidgetCreationFailed = 500,
  WidgetNotFound = 501,

  // Network / remote-provider errors (600-699)
  // The remote provider has no entry for the queried item — an HTTP 404, or
  // an empty "no match" result a provider classified as not-found. A routine,
  // expected scrape outcome, counted apart from genuine errors so a library
  // with some unmatched items doesn't inflate the error count (Kartend-e8aag).
  RemoteResourceNotFound = 600,

  // Kart packaging errors (700-799)
  KartFormatInvalid = 700,
  KartVersionUnsupported = 701,
  KartManifestParseFailed = 702,
  KartManifestInvalid = 703,
  KartIntegrityCheckFailed = 704,
  KartCompressionFailed = 705,
  KartEntryReadFailed = 706,

  // General errors (900-999)
  InvalidArgument = 900,
  OperationCancelled = 901,
  ResponseTooLarge = 902,
  // A bounded resource (e.g. the decompressed-size cap on launch-time
  // archive extraction, Kartend-ijglg) was exhausted and the operation was
  // aborted to protect the host.
  ResourceLimitExceeded = 903,
  UnknownError = 999
};

// Structured error context for detailed error reporting
struct ErrorContext {
  ErrorCode code = ErrorCode::Success;
  Severity severity = Severity::Error;
  QString message;
  QString details;
  QString source; // e.g., "QueryManager::fetchItemCount"
  // HTTP status code captured by the network layer when the failure
  // originated from an HTTP transport. 0 = not an HTTP error / unknown.
  // Lets HTTP-aware callers (ScreenScraperProvider) re-map the generic
  // "HTTP request failed" message into provider-specific guidance
  // ("daily quota exhausted", "version blacklisted", etc.) without
  // having to scrape it back out of the details string.
  int httpStatus = 0;
  // Server-supplied retry hint (seconds) for transient throttling
  // failures (HTTP 429 Retry-After). 0 = absent. Callers can use this
  // to auto-pace re-attempts instead of failing the asset outright.
  int retryAfterSeconds = 0;

  [[nodiscard]] bool isError() const { return code != ErrorCode::Success; }
  [[nodiscard]] bool isCritical() const { return severity == Severity::Critical; }

  // Create success context (no error)
  static ErrorContext success() { return ErrorContext{}; }

  // Create informational context (not an error, just a condition)
  static ErrorContext info(ErrorCode code, const QString &message,
                           const QString &source = QString()) {
    return ErrorContext{code, Severity::Info, message, QString(), source};
  }

  // Create error context
  static ErrorContext error(ErrorCode code, const QString &message,
                            const QString &source = QString()) {
    return ErrorContext{code, Severity::Error, message, QString(), source};
  }

  // Create warning context
  static ErrorContext warning(ErrorCode code, const QString &message,
                              const QString &source = QString()) {
    return ErrorContext{code, Severity::Warning, message, QString(), source};
  }

  // Create critical error context
  static ErrorContext critical(ErrorCode code, const QString &message,
                               const QString &source = QString()) {
    return ErrorContext{code, Severity::Critical, message, QString(), source};
  }

  // Add details to existing context
  ErrorContext &withDetails(const QString &detailsText) {
    details = detailsText;
    return *this;
  }
  ErrorContext &withHttpStatus(int status) {
    httpStatus = status;
    return *this;
  }
  ErrorContext &withRetryAfter(int seconds) {
    retryAfterSeconds = seconds;
    return *this;
  }

  // One-line, human-facing summary that folds the HTTP status and a short
  // server-detail snippet into the lead message — so a failure list shows
  // "HTTP request failed (HTTP 404): Not Found" instead of a context-free
  // "HTTP request failed" (Kartend-e6oyu). Provider-remapped messages that
  // already embed an "(HTTP n)" code (e.g. ScreenScraper's) are left intact:
  // the status isn't re-appended and the raw (often foreign-language) response
  // body isn't tacked on.
  [[nodiscard]] QString userFacingSummary() const {
    QString summary = message;
    const bool messageHasStatus =
        httpStatus > 0 && message.contains(QStringLiteral("HTTP %1").arg(httpStatus));
    if (httpStatus > 0 && !messageHasStatus) {
      summary += QStringLiteral(" (HTTP %1)").arg(httpStatus);
    }
    // Only fold a detail snippet into generic messages — a provider message
    // that already carries an "(HTTP n)" code is a self-contained sentence.
    if (!details.isEmpty() && !message.contains(QStringLiteral("(HTTP "))) {
      const QString firstLine = details.section(QLatin1Char('\n'), 0, 0).trimmed();
      if (!firstLine.isEmpty() && !summary.contains(firstLine)) {
        summary += QStringLiteral(": %1").arg(firstLine);
      }
    }
    return summary;
  }
};

// Result type combining a value with optional error context.
// Class-level [[nodiscard]]: discarding any returned Result (notably
// Result<void> from validators) silently bypasses the structured error model,
// so make ignoring one a compile-time warning (Kartend-pftc).
template <typename T> class [[nodiscard]] Result {
public:
  Result(T value) : m_value(std::move(value)), m_error() {} // NOLINT(google-explicit-constructor)
  Result(ErrorContext error)                                // NOLINT(google-explicit-constructor)
      : m_value(std::nullopt), m_error(std::move(error)) {}

  [[nodiscard]] bool isOk() const { return m_value.has_value(); }
  [[nodiscard]] bool isError() const { return !m_value.has_value(); }

  // Precondition: isOk() must be true. std::optional::value() throws
  // std::bad_optional_access on the error path, which surfaces precondition
  // violations as exceptions instead of UB. Callers that haven't checked
  // isOk() first are at fault; valueOr() is the safe alternative.
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  [[nodiscard]] const T &value() const { return m_value.value(); }
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  [[nodiscard]] T &value() { return m_value.value(); }
  [[nodiscard]] T valueOr(T defaultValue) const {
    return m_value.value_or(std::move(defaultValue));
  }

  [[nodiscard]] const ErrorContext &error() const { return m_error; }

  // Convenience for checking specific error codes
  [[nodiscard]] bool hasErrorCode(ErrorCode code) const {
    return isError() && m_error.code == code;
  }

private:
  std::optional<T> m_value;
  ErrorContext m_error;
};

// Specialization for void result (just success/failure)
template <> class [[nodiscard]] Result<void> {
public:
  Result() : m_error() {}
  Result(ErrorContext error) : m_error(std::move(error)) {} // NOLINT(google-explicit-constructor)

  [[nodiscard]] bool isOk() const { return !m_error.isError(); }
  [[nodiscard]] bool isError() const { return m_error.isError(); }
  [[nodiscard]] const ErrorContext &error() const { return m_error; }
  [[nodiscard]] bool hasErrorCode(ErrorCode code) const {
    return m_error.isError() && m_error.code == code;
  }

  static Result success() { return Result(); }

private:
  ErrorContext m_error;
};

// Log error context using Qt's logging system
inline void logError(const ErrorContext &ctx) {
  QString msg = ctx.message;
  if (!ctx.source.isEmpty()) {
    msg = QString("[%1] %2").arg(ctx.source, ctx.message);
  }
  if (!ctx.details.isEmpty()) {
    // details is the one field that routinely carries remote text (HTTP
    // error bodies); flatten it at the sink so an embedded CR/LF can't
    // forge additional kartend.errors lines (log-injection hardening).
    msg += QString(" (%1)").arg(sanitizedSingleLine(ctx.details));
  }

  switch (ctx.severity) {
  case Severity::Info:
    qCInfo(lcErrors) << msg;
    break;
  case Severity::Warning:
    qCWarning(lcErrors) << msg;
    break;
  case Severity::Error:
    qCWarning(lcErrors) << "ERROR:" << msg;
    break;
  case Severity::Critical:
    qCCritical(lcErrors) << msg;
    break;
  }
}

// Convert error code to human-readable string
[[nodiscard]] inline QString errorCodeToString(ErrorCode code) {
  switch (code) {
  case ErrorCode::Success:
    return "Success";
  case ErrorCode::DatabaseConnectionFailed:
    return "DatabaseConnectionFailed";
  case ErrorCode::DatabaseQueryFailed:
    return "DatabaseQueryFailed";
  case ErrorCode::DatabaseTransactionFailed:
    return "DatabaseTransactionFailed";
  case ErrorCode::DatabaseNotOpen:
    return "DatabaseNotOpen";
  case ErrorCode::DatabaseConnectionLost:
    return "DatabaseConnectionLost";
  case ErrorCode::DatabaseConnectionRestored:
    return "DatabaseConnectionRestored";
  case ErrorCode::InvalidCollectionContext:
    return "InvalidCollectionContext";
  case ErrorCode::CollectionNotFound:
    return "CollectionNotFound";
  case ErrorCode::MediaDirectoryNotFound:
    return "MediaDirectoryNotFound";
  case ErrorCode::ArtworkDirectoryNotFound:
    return "ArtworkDirectoryNotFound";
  case ErrorCode::FileNotFound:
    return "FileNotFound";
  case ErrorCode::FileReadError:
    return "FileReadError";
  case ErrorCode::FileWriteError:
    return "FileWriteError";
  case ErrorCode::InvalidFilePath:
    return "InvalidFilePath";
  case ErrorCode::ConfigLoadFailed:
    return "ConfigLoadFailed";
  case ErrorCode::ConfigSaveFailed:
    return "ConfigSaveFailed";
  case ErrorCode::InvalidConfigValue:
    return "InvalidConfigValue";
  case ErrorCode::WidgetCreationFailed:
    return "WidgetCreationFailed";
  case ErrorCode::WidgetNotFound:
    return "WidgetNotFound";
  case ErrorCode::RemoteResourceNotFound:
    return "RemoteResourceNotFound";
  case ErrorCode::KartFormatInvalid:
    return "KartFormatInvalid";
  case ErrorCode::KartVersionUnsupported:
    return "KartVersionUnsupported";
  case ErrorCode::KartManifestParseFailed:
    return "KartManifestParseFailed";
  case ErrorCode::KartManifestInvalid:
    return "KartManifestInvalid";
  case ErrorCode::KartIntegrityCheckFailed:
    return "KartIntegrityCheckFailed";
  case ErrorCode::KartCompressionFailed:
    return "KartCompressionFailed";
  case ErrorCode::KartEntryReadFailed:
    return "KartEntryReadFailed";
  case ErrorCode::InvalidArgument:
    return "InvalidArgument";
  case ErrorCode::OperationCancelled:
    return "OperationCancelled";
  case ErrorCode::ResponseTooLarge:
    return "ResponseTooLarge";
  case ErrorCode::ResourceLimitExceeded:
    return "ResourceLimitExceeded";
  case ErrorCode::UnknownError:
    return "UnknownError";
  }
  // The switch above has no default and covers every enumerator, so -Wswitch
  // flags a newly-added ErrorCode at compile time instead of letting it fall
  // through to a generic string. This point is reachable only via an
  // out-of-range cast (Kartend-ww6k).
  Q_UNREACHABLE();
}

} // namespace ErrorUtils

// Allow ErrorContext to be used in queued signal/slot connections
// (cross-thread)
Q_DECLARE_METATYPE(ErrorUtils::ErrorContext)

#endif // ERRORUTILS_H
