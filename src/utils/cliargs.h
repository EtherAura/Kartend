#ifndef CLIARGS_H
#define CLIARGS_H

#include <QString>
#include <QStringList>

namespace CliArgs {

struct StartupOptions {
  // Kartend-z3w: when non-empty, names a collection that overrides the
  // persisted startupCollection setting for this launch. The startup logic
  // falls back to the default selection if the name doesn't match an
  // existing collection.
  QString collectionOverride;
};

[[nodiscard]] StartupOptions parseStartupArguments(const QStringList &arguments);

} // namespace CliArgs

#endif // CLIARGS_H
