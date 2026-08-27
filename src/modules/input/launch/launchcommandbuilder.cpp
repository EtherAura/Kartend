// Pure command/parameter construction split out of launchmanager.cpp (see
// launchcommandbuilder.h for the seam rationale). The Result-returning
// validators (validatePathSecurity / validateLauncherPath) deliberately stay
// on LaunchManager; the preview path calls back into them (and into
// isArchiveFile) so the dry-run keeps judging exactly what a real launch
// would execute.
#include "launchcommandbuilder.h"
#include "collection/launcherconfig.h"
#include "kartlink.h"
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
    // Name the collection and say what to do (Kartend-6tj2v). The bare "No
    // launcher configured" was the FIRST thing a user saw after importing an
    // ES-DE library — those collections ship without a launcher by design,
    // since ES-DE keeps its emulator settings where Kartend cannot read them —
    // and a red error with no next step reads as a broken import rather than
    // an unfinished setup.
    return ErrorContext::error(ErrorCode::InvalidArgument,
                               QObject::tr("No launcher is set for \"%1\" — choose one in "
                                           "Settings › Collections › Launcher.")
                                   .arg(collectionName),
                               "LaunchCommandBuilder::buildLaunchCommand")
        .withDetails(QString("Collection '%1'").arg(collectionName));
  }

  // Launcher-import stubs (Kartend-wuq2c): a .kartlink file is a shortcut
  // written by LauncherImportService for a game installed through an external
  // launcher; the launcher must receive the stub's TARGET (a steam:// URI, a
  // Flatpak app id, …), never the stub's own path. Resolving here — the one
  // seam both the real launch and the preview dry-run flow through — makes
  // the %1/%f substitution and append-at-end fallback below operate on the
  // target transparently, and keeps the preview honest about what executes.
  QString mediaArgument = filePath;
  QStringList stubArguments;
  if (KartLink::isKartLinkPath(filePath)) {
    auto link = KartLink::read(filePath);
    if (link.isError()) {
      return link.error();
    }
    mediaArgument = link.value().target;
    // Kartend-4cff2: a stub may carry extra arguments for launchers whose
    // invocation has more than one variable part (Bottles needs the bottle
    // name alongside the program). Each entry becomes exactly one argv slot —
    // they are never re-split — so a value containing spaces cannot introduce
    // an argument boundary.
    //
    // Kartend-1o1a1: taken ONLY from a stub under Kartend's managed import
    // root. This list is an argv-flag primitive — its entries land verbatim in
    // the launcher's option set — and character-level validation cannot police
    // it, because an option flag is a legitimate value here. The previous
    // justification ("the importer wrote it") was not a security property: the
    // reader accepts any .kartlink a media scan finds, and a scanned directory
    // is somewhere an attacker who can drop a file already controls. Location
    // is the one provenance signal that writing a file cannot forge, so that
    // is what is checked. A stub found elsewhere still launches — its target is
    // validated and dash-guarded below — it just contributes no flags.
    if (KartLink::isManagedStubPath(filePath)) {
      stubArguments = link.value().args;
    }
  }

  // Validate the media argument (file path, or resolved shortcut target) for
  // security. Existence is not checked here; only character-level checks —
  // URI targets like steam://rungameid/220 pass by design.
  auto fileValidation = PathUtils::validatePathSecurity(mediaArgument);
  if (fileValidation.isError()) {
    return fileValidation.error();
  }
  // Same character-level check for the stub's own arguments. The leading-dash
  // guard below deliberately does NOT apply to them: an option flag is the
  // whole point of the list. That exemption is why the list needed a
  // provenance gate above and not merely a stronger character rule — no
  // character-level test can separate a legitimate flag from a hostile one.
  // Trusted provenance is not a pass for metacharacters, though: a managed
  // stub's args still go through this loop.
  for (const QString &argument : stubArguments) {
    auto argumentValidation = PathUtils::validatePathSecurity(argument);
    if (argumentValidation.isError()) {
      return argumentValidation.error();
    }
  }

  // The media path is appended as the final argument (both the libretro and
  // plain-launcher branches below). A path whose passed form starts with '-'
  // would be parsed by the launcher as an option, not a file operand
  // (argv-flag injection — no shell is involved, but launcher flags could be
  // flipped by an oddly/maliciously named file). Reject it, mirroring the
  // corePath leading-dash guard below. Absolute paths (the normal case) start
  // with '/', so this never triggers for them.
  if (mediaArgument.startsWith('-')) {
    return ErrorContext::error(ErrorCode::InvalidFilePath, "Media path cannot start with a dash",
                               "LaunchCommandBuilder::buildLaunchCommand")
        .withDetails(
            QString("File path '%1' would be parsed as a launcher option").arg(mediaArgument));
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
  // Kartend-kvcxd: derived path tokens. %1/%f hand the launcher the whole
  // media argument; these name its parts, which is what a template needs when
  // the launcher wants a title, a working directory, or a sibling file — the
  // only alternative was wrapping every launcher in a shell script.
  //
  // %name% follows the GRID's display title (completeBaseName of the item's own
  // path), so a .kartlink stub yields "Half-Life 2" rather than a fragment of
  // its steam:// target. The other three describe a real file on disk and are
  // therefore deliberately empty for a stub: a launcher-import entry has no
  // media file whose directory or extension could be named. previewLaunchCommand
  // warns when a template asks for them anyway.
  const QFileInfo mediaInfo(filePath);
  const bool isLauncherStub = KartLink::isKartLinkPath(filePath);
  const QString nameToken = mediaInfo.completeBaseName();
  const QString fileNameToken = isLauncherStub ? QString() : mediaInfo.fileName();
  const QString dirToken = isLauncherStub ? QString() : mediaInfo.absolutePath();
  const QString extToken = isLauncherStub ? QString() : mediaInfo.suffix();

  // The same argv-flag guard the media path gets above, applied to whichever
  // derived values the template actually names: a file called "-rf.mkv" yields
  // %name% == "-rf", which a launcher reads as an option rather than a value.
  // Substitution happens inside an already-split token so this can never add an
  // argument, but it can still flip a flag. Only tokens the template mentions
  // are checked, so a pathological filename cannot break launches whose
  // template never asks for that part of it.
  const QString rawParameters = launcher.launchParameters.trimmed();
  const QList<QPair<QString, QString>> derivedTokens{{QStringLiteral("%name%"), nameToken},
                                                     {QStringLiteral("%filename%"), fileNameToken},
                                                     {QStringLiteral("%dir%"), dirToken},
                                                     {QStringLiteral("%ext%"), extToken}};
  for (const auto &[token, value] : derivedTokens) {
    if (value.startsWith('-') && rawParameters.contains(token, Qt::CaseInsensitive)) {
      return ErrorContext::error(ErrorCode::InvalidFilePath,
                                 QObject::tr("%1 would expand to \"%2\", which the launcher would "
                                             "read as an option rather than a value.")
                                     .arg(token, value),
                                 "LaunchCommandBuilder::buildLaunchCommand")
          .withDetails(QString("Token '%1' from file path '%2'").arg(token, filePath));
    }
  }

  bool sawFilePlaceholder = false;
  // Kartend-li94g: the libretro branch's counterpart to sawFilePlaceholder —
  // true once a template has positioned the core itself, so the branch knows
  // not to append a second `-L <core>` on top of the one it just expanded.
  bool sawCorePlaceholder = false;
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
      // Derived tokens before %1/%f: each is %word%-delimited, so a plain
      // case-insensitive replace is enough — no \b needed, and "%name%" is not
      // a substring of "%filename%", so the two cannot alias regardless of
      // order. Doing them ahead of the media path also keeps a path that
      // happens to contain a literal token from being re-substituted.
      for (const auto &[token, value] : derivedTokens) {
        arg.replace(token, value, Qt::CaseInsensitive);
      }
      if (arg.contains(kFileTokenRe)) {
        sawFilePlaceholder = true;
        arg.replace(kFileTokenRe, mediaArgument);
      }
      if (arg.contains(kCoreTokenRe)) {
        sawCorePlaceholder = true;
        arg.replace(kCoreTokenRe, expandedCorePath);
      }
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
    // Kartend-li94g: append only the halves the template did NOT already
    // place. This used to append the whole `-L <core> <media>` triple
    // unconditionally, which double-emitted it for every launcher carrying the
    // placeholders — including the probe's own RetroArch default,
    // `-L %core "%1"` (launcherprobe.cpp), i.e. the normal wizard-seeded path.
    // Observed argv was `-L core rom -L core rom`. RetroArch tolerates that,
    // which is why it went unnoticed, but a template naming a DIFFERENT core
    // had the collection's corePath appended after its own, silently competing
    // with an explicit choice.
    //
    // The plain-launcher branch below has always guarded the media half this
    // way; the libretro branch set sawFilePlaceholder and then ignored it.
    // Order is unchanged for the common "template carries neither" case, which
    // still produces exactly `[params…] -L <core> <media>`.
    if (!sawCorePlaceholder) {
      cmd.arguments << "-L" << expandedCorePath;
    }
    if (!sawFilePlaceholder) {
      cmd.arguments << mediaArgument;
    }
    cmd.arguments << stubArguments; // normally empty; never silently dropped
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
    cmd.arguments << mediaArgument;
  }
  // Stub arguments last: they close the invocation (Bottles' list ends in the
  // `--` that terminates option parsing), so they must follow both the
  // template's arguments and the appended media path.
  cmd.arguments << stubArguments;
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

  // Kartend-kvcxd: %filename%/%dir%/%ext% describe a media file on disk, which
  // a launcher-import stub does not have — they expand to empty rather than to
  // a fragment of the stub's steam:// target. Say so here, or the emptied
  // argument in the preview above reads as a Kartend bug. %name% is absent from
  // this list on purpose: it resolves to the display title for stubs too.
  if (KartLink::isKartLinkPath(filePath)) {
    static const QStringList kPathPartTokens{QStringLiteral("%filename%"), QStringLiteral("%dir%"),
                                             QStringLiteral("%ext%")};
    for (const QString &token : kPathPartTokens) {
      if (launcher.launchParameters.contains(token, Qt::CaseInsensitive)) {
        out.warnings << QObject::tr("%1 is empty for launcher-import entries — they launch through "
                                    "a shortcut and have no media file on disk.")
                            .arg(token);
        break; // one is enough; the argument list already shows the effect
      }
    }
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
  // Set when a quote opens so an explicitly quoted empty argument ("" or '')
  // still produces a token instead of being silently dropped.
  bool tokenStarted = false;
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
      tokenStarted = true;
      quoteChar = currentChar;
    } else if (inQuotes && currentChar == quoteChar) {
      inQuotes = false;
    } else if (currentChar == ' ' && !inQuotes) {
      if (tokenStarted || !currentParam.isEmpty()) {
        result.append(currentParam);
        currentParam.clear();
        tokenStarted = false;
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

  if (tokenStarted || !currentParam.isEmpty()) {
    result.append(currentParam);
  }

  return result;
}

} // namespace LaunchCommandBuilder
