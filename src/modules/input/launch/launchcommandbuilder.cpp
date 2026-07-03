// Pure command/parameter construction split out of launchmanager.cpp (see
// launchcommandbuilder.h for the seam rationale). The Result-returning
// validators (validatePathSecurity / validateLauncherPath) deliberately stay
// on LaunchManager; the preview path calls back into them (and into
// isArchiveFile) so the dry-run keeps judging exactly what a real launch
// would execute.
#include "launchcommandbuilder.h"
#include "collection/launcherconfig.h"
#include "launchmanager.h"
#include "pathutils.h"

#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace LaunchCommandBuilder {

auto buildLaunchCommand(const LauncherConfig &launcher, const QString &collectionName,
                        const QString &filePath) -> ErrorUtils::Result<LaunchCommand> {
  // Reject collection names that would inject `..`, `/`, or `\` segments into
  // the `%collection%` substitution. Defence-in-depth against malicious or
  // mistyped names entering via kart import or settings edits.
  auto nameValidation = PathUtils::validateCollectionNameForSubstitution(collectionName);
  if (nameValidation.isError()) {
    return nameValidation.error();
  }

  // Shared with the pre-launch gate (LauncherUtils::launcherPathIssues'
  // launch-time overload) so validation judges exactly the paths built here.
  const QString expandedLauncherPath =
      LauncherUtils::expandCollectionPlaceholder(launcher.launcherPath, collectionName);
  const QString expandedCorePath =
      LauncherUtils::expandCollectionPlaceholder(launcher.corePath, collectionName);

  if (expandedLauncherPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "No launcher configured",
                               "LaunchCommandBuilder::buildLaunchCommand")
        .withDetails(QString("Collection '%1'").arg(collectionName));
  }

  // Validate media file path for security.
  // Existence is not checked here; only character-level security checks.
  auto fileValidation = PathUtils::validatePathSecurity(filePath);
  if (fileValidation.isError()) {
    return fileValidation.error();
  }

  // The media path is appended as the final argument (both the libretro and
  // plain-launcher branches below). A path whose passed form starts with '-'
  // would be parsed by the launcher as an option, not a file operand
  // (argv-flag injection — no shell is involved, but launcher flags could be
  // flipped by an oddly/maliciously named file). Reject it, mirroring the
  // corePath leading-dash guard below. Absolute paths (the normal case) start
  // with '/', so this never triggers for them.
  if (filePath.startsWith('-')) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Media path cannot start with a dash",
                               "LaunchCommandBuilder::buildLaunchCommand")
        .withDetails(QString("File path '%1' would be parsed as a launcher option").arg(filePath));
  }

  LaunchCommand cmd;
  cmd.program = expandedLauncherPath;

  // Parse + expand the user's launch parameters once, shared by both the
  // libretro and plain-launcher branches below (Kartend-q21fy).
  //
  // Kartend-nv9iw: tokenize the RAW template, THEN substitute %collection%
  // inside each already-split argument. Expanding before tokenizing let a
  // collection name containing spaces or a leading dash (which can arrive from
  // an imported .kart manifest) split into extra argv entries — injecting
  // attacker-chosen flags into the launcher. Per-token substitution can never
  // introduce a new argument boundary.
  //
  // Kartend-51d3e: the same per-token pass substitutes the media-path
  // placeholders (%1, %f) and the core placeholder (%core), so probe-/wizard-
  // seeded templates like ffmpeg's `-autoexit -nodisp "%1"` or RetroArch's
  // `-L %core "%1"` land the real paths in the placeholder position instead of
  // passing a literal token through. Replacing inside the already-split token
  // keeps the argv boundary the template author chose — `"%1"` stays one
  // argument even when filePath contains spaces.
  bool sawFilePlaceholder = false;
  // Boundary-aware placeholder tokens, mirroring previewLaunchCommand's
  // unresolved-placeholder regex (kPlaceholderRe below). Plain substring
  // replacement mangled longer tokens: `%f` matched inside %file% /
  // %fullscreen% (corrupting the argument AND wrongly setting
  // sawFilePlaceholder, which suppresses the append-media-path fallback),
  // `%core` matched inside %cores% / %coreopts%, and `%1` inside %10. The
  // trailing \b leaves a token followed by another word character alone, so
  // the preview's warning and the executed command agree on what substitutes.
  // %collection% needs no boundary — its trailing '%' already delimits it.
  static const QRegularExpression kFileTokenRe(QStringLiteral("%(?:1|f)\\b"),
                                               QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression kCoreTokenRe(QStringLiteral("%core\\b"),
                                               QRegularExpression::CaseInsensitiveOption);
  auto expandLaunchParameters = [&](QStringList &out) -> ErrorUtils::Result<void> {
    const QString rawLaunchParameters = launcher.launchParameters.trimmed();
    if (rawLaunchParameters.isEmpty()) {
      return {};
    }
    auto parseResult = parseParameters(rawLaunchParameters);
    if (parseResult.isError()) {
      return parseResult.error();
    }
    QStringList expandedArgs = parseResult.value();
    for (QString &arg : expandedArgs) {
      arg.replace("%collection%", collectionName, Qt::CaseInsensitive);
      if (arg.contains(kFileTokenRe)) {
        sawFilePlaceholder = true;
        arg.replace(kFileTokenRe, filePath);
      }
      arg.replace(kCoreTokenRe, expandedCorePath);
    }
    out.append(expandedArgs);
    return {};
  };

  if (LauncherUtils::usesLibretroCore(expandedLauncherPath)) {
    if (expandedCorePath.isEmpty()) {
      return ErrorContext::error(ErrorCode::InvalidArgument, "No libretro core configured",
                                 "LaunchCommandBuilder::buildLaunchCommand")
          .withDetails(QString("Collection '%1'").arg(collectionName));
    }

    // Core path should be a file path, not a flag.
    if (expandedCorePath.startsWith("-")) {
      return ErrorContext::error(ErrorCode::InvalidFilePath, "Core path cannot start with a dash",
                                 "LaunchCommandBuilder::buildLaunchCommand")
          .withDetails(QString("Core path '%1' looks like an option").arg(expandedCorePath));
    }

    auto coreValidation = PathUtils::validatePathSecurity(expandedCorePath);
    if (coreValidation.isError()) {
      return coreValidation.error();
    }

    // Kartend-q21fy: honor the user's launch parameters on the libretro path
    // too. They were previously discarded silently — a configured
    // --fullscreen / --config override simply never took effect. Insert the
    // parsed + expanded tokens AHEAD of the `-L <core> <file>` triple so the
    // triple ordering RetroArch expects stays intact.
    auto paramResult = expandLaunchParameters(cmd.arguments);
    if (paramResult.isError()) {
      return paramResult.error();
    }
    cmd.arguments << "-L" << expandedCorePath << filePath;
    return cmd;
  }

  // Plain launcher: parse optional launch parameters string.
  auto paramResult = expandLaunchParameters(cmd.arguments);
  if (paramResult.isError()) {
    return paramResult.error();
  }
  // Only fall back to appending the media path when no %1/%f placeholder set
  // its position explicitly — this preserves the historical append-at-end
  // behavior for the common templates that carry no media placeholder.
  if (!sawFilePlaceholder) {
    cmd.arguments << filePath;
  }
  return cmd;
}

auto previewLaunchCommand(const CollectionConfig &collection, const LauncherConfig &launcher,
                          const QString &filePath) -> LaunchPreview {
  LaunchPreview out;
  // Expand %collection% so the build-error early-return below reports the
  // resolved program rather than the raw template (the success path overwrites
  // this with cmd.value().program, which is the same expanded value).
  out.program =
      QString(launcher.launcherPath).replace("%collection%", collection.name, Qt::CaseInsensitive);

  auto cmd = buildLaunchCommand(launcher, collection.name, filePath);
  if (cmd.isError()) {
    out.buildOk = false;
    out.buildError = cmd.error().message;
    out.warnings << cmd.error().message;
    // Still report the file-existence + archive bits the user can fix from
    // the preview surface even when the command can't be assembled.
    out.fileExists = QFileInfo::exists(filePath);
    if (collection.archive.extractArchives && LaunchManager::isArchiveFile(filePath)) {
      out.wouldExtractArchive = true;
      out.archiveTargetExtension = collection.archive.extractedExtension;
    }
    return out;
  }

  out.buildOk = true;
  out.program = cmd.value().program;
  out.arguments = cmd.value().arguments;

  // Resolve the launcher to an absolute executable so the preview shows
  // both the configured value and what the OS would actually invoke. An
  // empty resolved path => not on PATH and not at the given absolute
  // location; surface as a warning.
  auto resolved = LaunchManager::validateLauncherPath(out.program);
  if (resolved.isOk()) {
    out.resolvedProgram = resolved.value();
  } else {
    out.warnings << QObject::tr("Launcher executable not found: %1").arg(out.program);
  }

  out.fileExists = QFileInfo::exists(filePath);
  if (!out.fileExists) {
    out.warnings << QObject::tr("File does not exist on disk: %1").arg(filePath);
  }

  // Archive-extraction warnings. When the toggle is on for the collection
  // and the file is recognised as an archive, the launcher would receive
  // the extracted file path at runtime instead of the original archive —
  // call that out explicitly so the preview doesn't lie about what gets
  // executed. The extension being empty is its own actionable warning.
  if (collection.archive.extractArchives && LaunchManager::isArchiveFile(filePath)) {
    out.wouldExtractArchive = true;
    out.archiveTargetExtension = collection.archive.extractedExtension;
    if (out.archiveTargetExtension.trimmed().isEmpty()) {
      out.warnings << QObject::tr(
          "Archive extraction is enabled but the target extension is empty — "
          "the launcher would receive the archive path verbatim.");
    }
  }

  // Kartend-pgfks: a configured core path is only ever consumed by the
  // libretro branch of buildLaunchCommand (which keys off the launcher's
  // basename). When the launcher isn't classified libretro the core path is
  // silently dropped at launch — surface that here so a stray core path (e.g.
  // left over from a rename or arriving via an imported bundle) is visible in
  // the preview rather than mysteriously ignored. Preview-only; the actual
  // command above is unchanged.
  const QString previewCorePath =
      QString(launcher.corePath).replace("%collection%", collection.name, Qt::CaseInsensitive);
  if (!previewCorePath.trimmed().isEmpty() && !LauncherUtils::usesLibretroCore(out.program)) {
    out.warnings << QObject::tr(
                        "Core path will be ignored (launcher is not a libretro core launcher): %1")
                        .arg(previewCorePath.trimmed());
  }

  // Heuristic unresolved-placeholder check. We've already substituted
  // %collection%, %1/%f (media path) and %core; any remaining placeholder —
  // a %name% style token, or a bare %1/%f/%core that was never substituted
  // (e.g. %core left in a non-libretro launcher) — likely indicates a typo or
  // a placeholder the launcher won't honour. Flag it without claiming to know
  // what the user meant. Must stay in sync with the boundary-aware
  // substitution tokens in buildLaunchCommand (kFileTokenRe / kCoreTokenRe)
  // so preview warnings and the executed command never disagree.
  static const QRegularExpression kPlaceholderRe(
      QStringLiteral("%[A-Za-z0-9_]+%|%(?:[0-9]+|f|core)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  for (const QString &arg : out.arguments) {
    auto m = kPlaceholderRe.match(arg);
    if (m.hasMatch()) {
      out.warnings << QObject::tr("Unresolved placeholder in argument: %1").arg(m.captured(0));
      break; // one warning is enough; the dialog can show the full args
    }
  }

  return out;
}

auto parseParameters(const QString &paramString) -> ErrorUtils::Result<QStringList> {
  QStringList result;
  if (paramString.trimmed().isEmpty()) {
    return result;
  }

  // Reject null bytes/newlines which can cause confusing log/diagnostic output.
  if (paramString.contains(QChar('\0')) || paramString.contains('\n') ||
      paramString.contains('\r')) {
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               "Launch parameters contain invalid control characters",
                               "LaunchCommandBuilder::parseParameters");
  }

  QString params = paramString.trimmed();
  bool inQuotes = false;
  QString currentParam;
  QChar quoteChar;

  for (int i = 0; i < params.length(); ++i) {
    QChar currentChar = params[i];

    // Backslash escapes the next character when that character is one the parser
    // would otherwise treat specially — a quote, a separator space, or a
    // backslash — so a param can carry a literal quote (Kartend-xi2mj). Works
    // inside and outside quotes. A backslash before any other character (e.g. a
    // path separator) is left literal so existing params are unaffected.
    if (currentChar == '\\' && i + 1 < params.length()) {
      const QChar next = params[i + 1];
      if (next == '"' || next == '\'' || next == ' ' || next == '\\') {
        currentParam.append(next);
        ++i;
        continue;
      }
    }

    if (!inQuotes && (currentChar == '"' || currentChar == '\'')) {
      inQuotes = true;
      quoteChar = currentChar;
    } else if (inQuotes && currentChar == quoteChar) {
      inQuotes = false;
    } else if (currentChar == ' ' && !inQuotes) {
      if (!currentParam.isEmpty()) {
        result.append(currentParam);
        currentParam.clear();
      }
    } else {
      currentParam.append(currentChar);
    }
  }

  // Check for unclosed quotes - potential injection vulnerability
  if (inQuotes) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Unclosed quote in launch parameters",
                               "LaunchCommandBuilder::parseParameters")
        .withDetails(QString("Quote character '%1' was not closed").arg(quoteChar));
  }

  if (!currentParam.isEmpty()) {
    result.append(currentParam);
  }

  return result;
}

} // namespace LaunchCommandBuilder
