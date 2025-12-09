#ifndef ERRORUTILS_H
#define ERRORUTILS_H

#include <QString>
#include <QDebug>
#include <optional>

namespace ErrorUtils {

// Error severity levels for logging and handling decisions
enum class Severity {
  Info,       // Informational, not an error
  Warning,    // Something unexpected but recoverable
  Error,      // An error that may affect functionality
  Critical    // A critical error that prevents operation
};

// Error codes for categorizing errors across the application
enum class ErrorCode {
  None = 0,
  
  // Database errors (100-199)
  DatabaseConnectionFailed = 100,
  DatabaseQueryFailed = 101,
  DatabaseTransactionFailed = 102,
  DatabaseNotOpen = 103,
  
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
  
  // General errors (900-999)
  InvalidArgument = 900,
  OperationCancelled = 901,
  UnknownError = 999
};

// Structured error context for detailed error reporting
struct ErrorContext {
  ErrorCode code = ErrorCode::None;
  Severity severity = Severity::Error;
  QString message;
  QString details;
  QString source;  // e.g., "QueryManager::fetchItemCount"
  
  [[nodiscard]] bool isError() const { return code != ErrorCode::None; }
  [[nodiscard]] bool isCritical() const { return severity == Severity::Critical; }
  
  // Create success context (no error)
  static ErrorContext success() {
    return ErrorContext{};
  }

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
};

// Result type combining a value with optional error context
template<typename T>
class Result {
public:
  Result(T value) : m_value(std::move(value)), m_error() {}
  Result(ErrorContext error) : m_value(std::nullopt), m_error(std::move(error)) {}
  
  [[nodiscard]] bool isOk() const { return m_value.has_value(); }
  [[nodiscard]] bool isError() const { return !m_value.has_value(); }
  
  [[nodiscard]] const T& value() const { return m_value.value(); }
  [[nodiscard]] T& value() { return m_value.value(); }
  [[nodiscard]] T valueOr(T defaultValue) const { 
    return m_value.value_or(std::move(defaultValue)); 
  }
  
  [[nodiscard]] const ErrorContext& error() const { return m_error; }
  
  // Convenience for checking specific error codes
  [[nodiscard]] bool hasErrorCode(ErrorCode code) const {
    return isError() && m_error.code == code;
  }

private:
  std::optional<T> m_value;
  ErrorContext m_error;
};

// Specialization for void result (just success/failure)
template<>
class Result<void> {
public:
  Result() : m_error() {}
  Result(ErrorContext error) : m_error(std::move(error)) {}
  
  [[nodiscard]] bool isOk() const { return !m_error.isError(); }
  [[nodiscard]] bool isError() const { return m_error.isError(); }
  [[nodiscard]] const ErrorContext& error() const { return m_error; }
  
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
    msg += QString(" (%1)").arg(ctx.details);
  }
  
  switch (ctx.severity) {
    case Severity::Info:
      qInfo() << msg;
      break;
    case Severity::Warning:
      qWarning() << msg;
      break;
    case Severity::Error:
      qWarning() << "ERROR:" << msg;
      break;
    case Severity::Critical:
      qCritical() << msg;
      break;
  }
}

// Convert error code to human-readable string
[[nodiscard]] inline QString errorCodeToString(ErrorCode code) {
  switch (code) {
    case ErrorCode::None: return "None";
    case ErrorCode::DatabaseConnectionFailed: return "DatabaseConnectionFailed";
    case ErrorCode::DatabaseQueryFailed: return "DatabaseQueryFailed";
    case ErrorCode::DatabaseTransactionFailed: return "DatabaseTransactionFailed";
    case ErrorCode::DatabaseNotOpen: return "DatabaseNotOpen";
    case ErrorCode::InvalidCollectionContext: return "InvalidCollectionContext";
    case ErrorCode::CollectionNotFound: return "CollectionNotFound";
    case ErrorCode::MediaDirectoryNotFound: return "MediaDirectoryNotFound";
    case ErrorCode::ArtworkDirectoryNotFound: return "ArtworkDirectoryNotFound";
    case ErrorCode::FileNotFound: return "FileNotFound";
    case ErrorCode::FileReadError: return "FileReadError";
    case ErrorCode::FileWriteError: return "FileWriteError";
    case ErrorCode::InvalidFilePath: return "InvalidFilePath";
    case ErrorCode::ConfigLoadFailed: return "ConfigLoadFailed";
    case ErrorCode::ConfigSaveFailed: return "ConfigSaveFailed";
    case ErrorCode::InvalidConfigValue: return "InvalidConfigValue";
    case ErrorCode::WidgetCreationFailed: return "WidgetCreationFailed";
    case ErrorCode::WidgetNotFound: return "WidgetNotFound";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::OperationCancelled: return "OperationCancelled";
    case ErrorCode::UnknownError: return "UnknownError";
  }
  return "Unknown";
}

} // namespace ErrorUtils

// Allow ErrorContext to be used in queued signal/slot connections (cross-thread)
Q_DECLARE_METATYPE(ErrorUtils::ErrorContext)

#endif // ERRORUTILS_H
