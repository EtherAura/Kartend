#include "cliargs.h"

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

  // parse() never exits on its own; process() would call exit() on unknown
  // options or --help. Using parse() keeps this function unit-testable. The
  // production caller in main.cpp uses process() so users still get the
  // standard --help / --version / unknown-option behavior on the real CLI.
  parser.parse(arguments);

  StartupOptions options;
  if (parser.isSet(collectionOption)) {
    options.collectionOverride = parser.value(collectionOption).trimmed();
  }
  return options;
}

} // namespace CliArgs
