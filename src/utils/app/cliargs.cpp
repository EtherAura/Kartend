#include "cliargs.h"

#include "pathutils.h"
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

namespace CliArgs {

StartupOptions parseStartupArguments(const QStringList &arguments) {
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QCoreApplication::translate("CliArgs", "Kartend - Qt6/KDE multimedia collection launcher."));
  parser.addHelpOption();
  parser.addVersionOption();

  QCommandLineOption collectionOption(
      QStringList{QStringLiteral("c"), QStringLiteral("collection")},
      QCoreApplication::translate(
          "CliArgs", "Open Kartend directly into the named collection, bypassing the "
                     "configured default. Falls back to the default if <name> is unknown."),
      QCoreApplication::translate("CliArgs", "name"));
  parser.addOption(collectionOption);

  QCommandLineOption importKartOption(
      QStringLiteral("import-kart"),
      QCoreApplication::translate("CliArgs", "Import a .kart package headlessly and exit."),
      QCoreApplication::translate("CliArgs", "path"));
  parser.addOption(importKartOption);

  QCommandLineOption toOption(
      QStringLiteral("to"),
      QCoreApplication::translate("CliArgs", "Destination directory for --import-kart."),
      QCoreApplication::translate("CliArgs", "dir"));
  parser.addOption(toOption);

  QCommandLineOption exportKartOption(
      QStringLiteral("export-kart"),
      QCoreApplication::translate("CliArgs", "Export the named collection headlessly and exit."),
      QCoreApplication::translate("CliArgs", "name"));
  parser.addOption(exportKartOption);

  QCommandLineOption exportOutOption(
      QStringLiteral("export-out"),
      QCoreApplication::translate("CliArgs", "Output path for --export-kart."),
      QCoreApplication::translate("CliArgs", "path"));
  parser.addOption(exportOutOption);

  QCommandLineOption onConflictOption(
      QStringLiteral("on-conflict"),
      QCoreApplication::translate("CliArgs",
                                  "Conflict policy for --import-kart: skip, overwrite, merge."),
      QCoreApplication::translate("CliArgs", "policy"), QStringLiteral("skip"));
  parser.addOption(onConflictOption);

  // parse() never exits on its own; process() would call exit() on unknown
  // options or --help. Using parse() keeps this function unit-testable. The
  // production caller in main.cpp uses process() so users still get the
  // standard --help / --version / unknown-option behavior on the real CLI.
  parser.parse(arguments);

  StartupOptions options;

  // Route --import-kart and --to through the CLI-seam path sanitizer so a
  // tilde gets expanded and shell metachars / NUL bytes get rejected
  // before MainWindow touches the values. On failure the raw value stays
  // in place and the error is stashed in pathValidationError — main.cpp
  // surfaces a single clear stderr line and exits non-zero. Existence is
  // NOT checked here; KartReader produces a more specific "Cannot open
  // Kart file" error than we could synthesize.
  auto sanitizeCliPath = [&options](const QString &raw, const QString &optionName) -> QString {
    auto result = PathUtils::expandAndValidateCliPath(raw, optionName);
    if (result.isError() && !options.pathValidationError.isError()) {
      options.pathValidationError = result.error();
    }
    return result.isOk() ? result.value() : raw;
  };

  if (parser.isSet(collectionOption)) {
    options.collectionOverride = parser.value(collectionOption).trimmed();
  }
  if (parser.isSet(importKartOption)) {
    options.importKartPath =
        sanitizeCliPath(parser.value(importKartOption), QStringLiteral("import-kart"));
  }
  if (parser.isSet(toOption)) {
    options.importDestDir = sanitizeCliPath(parser.value(toOption), QStringLiteral("to"));
  }
  if (parser.isSet(exportKartOption)) {
    options.exportCollectionName = parser.value(exportKartOption);
  }
  if (parser.isSet(exportOutOption)) {
    // Kartend-928mu: sanitize like the other CLI filesystem args so all share
    // one seam (was stored verbatim — no tilde expansion / metachar check).
    options.exportOutPath =
        sanitizeCliPath(parser.value(exportOutOption), QStringLiteral("export-out"));
  }
  if (parser.isSet(onConflictOption)) {
    const QString v = parser.value(onConflictOption).toLower();
    if (v == "overwrite") {
      options.onConflict = KartConflictPolicy::Overwrite;
    } else if (v == "merge") {
      options.onConflict = KartConflictPolicy::Merge;
    } else {
      options.onConflict = KartConflictPolicy::Skip;
    }
  }
  return options;
}

} // namespace CliArgs
