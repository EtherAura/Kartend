#include "archivesafety.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

namespace ArchiveSafety {

namespace {

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

constexpr const char *kSource = "ArchiveSafety::scanArchiveEntries";
/// Listing a legitimate archive is metadata-only and fast; a scan that takes
/// this long is either wedged storage or an adversarial input — both reject.
constexpr int kListTimeoutMs = 30000;
/// Cap on accepted listing output. A million-entry listing blows past this
/// long before it exhausts memory; real media archives stay far below it.
constexpr qint64 kMaxListingBytes = 16LL * 1024 * 1024;

/// Run `tool args` and return its stdout, bounded by the timeout and output
/// cap. Any abnormality (start failure, timeout, non-zero exit, oversized
/// listing) returns an error — callers fail closed.
ErrorUtils::Result<QByteArray> runListing(const QString &tool, const QStringList &args) {
  QProcess proc;
  proc.setProcessChannelMode(QProcess::SeparateChannels);
  proc.start(tool, args);
  if (!proc.waitForStarted(kListTimeoutMs)) {
    return ErrorContext::error(ErrorCode::UnknownError, "Could not start archive listing tool",
                               kSource)
        .withDetails(QString("%1: %2").arg(tool, proc.errorString()));
  }
  QByteArray out;
  QElapsedTimer clock;
  clock.start();
  while (!proc.waitForFinished(200)) {
    out += proc.readAllStandardOutput();
    if (out.size() > kMaxListingBytes) {
      proc.kill();
      proc.waitForFinished(2000);
      return ErrorContext::error(ErrorCode::ResponseTooLarge,
                                 "Archive listing exceeded the size cap", kSource);
    }
    if (clock.elapsed() >= kListTimeoutMs) {
      proc.kill();
      proc.waitForFinished(2000);
      return ErrorContext::error(ErrorCode::OperationCancelled, "Archive listing timed out",
                                 kSource);
    }
  }
  out += proc.readAllStandardOutput();
  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    return ErrorContext::error(ErrorCode::UnknownError, "Archive listing tool failed", kSource)
        .withDetails(QString::fromUtf8(proc.readAllStandardError()).left(300));
  }
  if (out.size() > kMaxListingBytes) {
    return ErrorContext::error(ErrorCode::ResponseTooLarge, "Archive listing exceeded the size cap",
                               kSource);
  }
  return out;
}

ErrorContext rejectEntry(const QString &reason, const QString &detail) {
  return ErrorContext::error(ErrorCode::InvalidFilePath, reason, kSource).withDetails(detail);
}

/// The listing tool exited 0 on a non-empty archive file, yet the parser
/// recognised zero entries — the loop above inspected NOTHING, and returning
/// success would fail OPEN on an output format we didn't understand (a
/// localized/reshaped listing, a tool update). FileReadError is deliberately
/// distinct from both the security class (InvalidFilePath — must abort, never
/// retry) and the tool-missing/unusable codes, so scanArchiveEntries' fallback
/// chain still consults the next tool while a lone-tool caller fails closed.
ErrorContext uninterpretableListing(const QString &tool) {
  return ErrorContext::error(ErrorCode::FileReadError,
                             "Archive listing could not be interpreted", kSource)
      .withDetails(QString("%1 reported success but no entries could be parsed "
                           "from its listing output")
                       .arg(tool));
}

/// bsdtar scan: `-tvf` exposes the mode string (symlinks lead with 'l',
/// hardlinks are marked "link to"), `-tf` yields one clean entry name per
/// line for the path-escape checks.
ErrorUtils::Result<void> scanWithBsdtar(const QString &archivePath) {
  const auto verbose = runListing(QStringLiteral("bsdtar"), {QStringLiteral("-tvf"), archivePath});
  if (verbose.isError()) return verbose.error();
  const QStringList verboseLines = QString::fromUtf8(verbose.value()).split(QLatin1Char('\n'));
  for (const QString &line : verboseLines) {
    if (line.isEmpty()) continue;
    if (line.startsWith(QLatin1Char('l'))) {
      return rejectEntry(QStringLiteral("Archive contains a symlink entry"), line.left(200));
    }
    if (line.startsWith(QLatin1Char('h')) || line.contains(QLatin1String(" link to "))) {
      return rejectEntry(QStringLiteral("Archive contains a hardlink entry"), line.left(200));
    }
  }
  const auto names = runListing(QStringLiteral("bsdtar"), {QStringLiteral("-tf"), archivePath});
  if (names.isError()) return names.error();
  const QStringList nameLines = QString::fromUtf8(names.value()).split(QLatin1Char('\n'));
  int inspectedEntries = 0;
  for (const QString &name : nameLines) {
    if (name.isEmpty()) continue;
    ++inspectedEntries;
    if (entryPathEscapes(name)) {
      return rejectEntry(QStringLiteral("Archive entry path escapes the extraction directory"),
                         name.left(200));
    }
  }
  // Zero parsed entries from a non-empty archive file must not scan as safe:
  // bsdtar's flat listing cannot distinguish "genuinely empty archive" from
  // "output we failed to parse", so refuse (fail closed) with a non-security
  // error and let the caller's fallback chain consult 7z, whose entry-table
  // separator CAN tell the two apart. (A zero-byte file never reaches here —
  // bsdtar exits non-zero on it, which runListing already rejects.)
  if (inspectedEntries == 0 && QFileInfo(archivePath).size() > 0) {
    return uninterpretableListing(QStringLiteral("bsdtar"));
  }
  return {};
}

/// 7z scan: `l -slt` emits key = value blocks per entry after a "----------"
/// separator (everything before it describes the archive itself). Symlinks
/// surface as a "Symbolic Link = target" field and/or an Attributes mode
/// string leading with 'l'; hardlinks as "Hard Link = target". The tar codec
/// emits BOTH keys with an empty value on every entry (regular files
/// included), so only a non-empty target is a link verdict — presence alone
/// would reject every tar archive.
ErrorUtils::Result<void> scanWith7z(const QString &archivePath) {
  const auto listing =
      runListing(QStringLiteral("7z"), {QStringLiteral("l"), QStringLiteral("-slt"), archivePath});
  if (listing.isError()) return listing.error();
  static const QRegularExpression kSymlinkMode(QStringLiteral("\\bl[rwxsStT-]{9}\\b"));
  bool inEntries = false;
  const QStringList lines = QString::fromUtf8(listing.value()).split(QLatin1Char('\n'));
  for (const QString &rawLine : lines) {
    const QString line = rawLine.trimmed();
    if (!inEntries) {
      inEntries = line.startsWith(QLatin1String("----------"));
      continue;
    }
    if (line.startsWith(QLatin1String("Path = "))) {
      const QString path = line.mid(7);
      if (entryPathEscapes(path)) {
        return rejectEntry(QStringLiteral("Archive entry path escapes the extraction directory"),
                           path.left(200));
      }
    } else if (line.startsWith(QLatin1String("Symbolic Link =")) &&
               !line.mid(15).trimmed().isEmpty()) {
      return rejectEntry(QStringLiteral("Archive contains a symlink entry"), line.left(200));
    } else if (line.startsWith(QLatin1String("Hard Link =")) && !line.mid(11).trimmed().isEmpty()) {
      // Mirrors the bsdtar scanner's hardlink rejection — the module
      // contract is tool-independent.
      return rejectEntry(QStringLiteral("Archive contains a hardlink entry"), line.left(200));
    } else if (line.startsWith(QLatin1String("Attributes = ")) &&
               kSymlinkMode.match(line).hasMatch()) {
      return rejectEntry(QStringLiteral("Archive contains a symlink entry"), line.left(200));
    }
  }
  // The "----------" separator is 7z's contract for where the per-entry
  // key = value table begins. If it never appeared, the loop above vetted
  // NOTHING — a zero-exit run whose output shape we didn't recognise must
  // fail closed rather than scan as safe (see uninterpretableListing). A
  // genuinely EMPTY archive passes: 7z still prints the separator for it,
  // just with no entry blocks after it.
  if (!inEntries && QFileInfo(archivePath).size() > 0) {
    return uninterpretableListing(QStringLiteral("7z"));
  }
  return {};
}

} // namespace

bool entryPathEscapes(const QString &entryName) {
  if (entryName.startsWith(QLatin1Char('/'))) return true;
  // Windows drive-absolute ("C:\...", "C:/...") or UNC ("\\host\share").
  if (entryName.size() >= 2 && entryName.at(1) == QLatin1Char(':')) return true;
  if (entryName.startsWith(QLatin1String("\\\\"))) return true;
  QString normalized = entryName;
  normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (normalized.startsWith(QLatin1Char('/'))) return true;
  const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  return parts.contains(QLatin1String(".."));
}

bool isSecurityRejection(const ErrorUtils::ErrorContext &err) {
  // rejectEntry() and the invalid-input guards below all use InvalidFilePath;
  // tool-could-not-list failures use UnknownError / ResponseTooLarge /
  // OperationCancelled / FileNotFound, and an uninterpretable-listing refusal
  // uses FileReadError. So InvalidFilePath is exactly the "unsafe — abort, do
  // not try another tool" class; every other code lets the caller fall
  // through to the next listing tool.
  return err.code == ErrorCode::InvalidFilePath;
}

ErrorUtils::Result<void> scanArchiveEntriesWithTool(const QString &archivePath,
                                                    const QString &tool) {
  // Positional-operand hygiene, mirroring the extraction sites: a leading
  // dash would be misparsed as an option by the listing tools.
  if (archivePath.isEmpty() || archivePath.startsWith(QLatin1Char('-'))) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Invalid archive path", kSource)
        .withDetails(archivePath);
  }
  if (QStandardPaths::findExecutable(tool).isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Archive listing tool not available",
                               kSource)
        .withDetails(tool);
  }
  if (tool == QLatin1String("bsdtar")) return scanWithBsdtar(archivePath);
  if (tool == QLatin1String("7z")) return scanWith7z(archivePath);
  return ErrorContext::error(ErrorCode::FileNotFound, "Unsupported archive listing tool", kSource)
      .withDetails(tool);
}

ErrorUtils::Result<void> scanArchiveEntries(const QString &archivePath) {
  // Try bsdtar, then 7z. A security rejection from any tool is returned
  // immediately (an unsafe entry is unsafe regardless of which tool saw it);
  // a tool that merely cannot LIST the format (e.g. bsdtar/libarchive on some
  // RAR5 variants) falls through to the next available tool so the archive is
  // still vetted rather than failing closed on a tool limitation.
  ErrorUtils::ErrorContext lastError =
      ErrorContext::error(ErrorCode::FileNotFound,
                          "No archive listing tool available for the safety scan", kSource)
          .withDetails(QStringLiteral("Install bsdtar or 7z"));
  for (const QString &tool : {QStringLiteral("bsdtar"), QStringLiteral("7z")}) {
    if (QStandardPaths::findExecutable(tool).isEmpty()) continue;
    const auto scan = scanArchiveEntriesWithTool(archivePath, tool);
    if (scan.isOk() || isSecurityRejection(scan.error())) return scan;
    lastError = scan.error(); // tool couldn't list it — remember and try the next
  }
  return lastError;
}

} // namespace ArchiveSafety
