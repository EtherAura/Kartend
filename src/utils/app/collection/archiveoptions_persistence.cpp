#include "archiveoptions_persistence.h"

#include "settingskeys.h"

namespace keys = kartend::settings::keys;

namespace ArchiveOptionsPersistence {

void load(QSettings &settings, ArchiveOptions &opts) {
  opts.extractArchives = settings.value(keys::kExtractArchives, false).toBool();
  opts.extractedExtension = settings.value(keys::kExtractedExtension).toString();
}

void save(QSettings &settings, const ArchiveOptions &opts) {
  settings.setValue(keys::kExtractArchives, opts.extractArchives);
  settings.setValue(keys::kExtractedExtension, opts.extractedExtension);
}

} // namespace ArchiveOptionsPersistence
