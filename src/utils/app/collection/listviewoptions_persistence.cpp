#include "listviewoptions_persistence.h"

#include "settingskeys.h"
#include "uiconstants/item.h"
#include "uiconstants/listview.h"

namespace keys = kartend::settings::keys;

namespace ListViewOptionsPersistence {

void load(QSettings &settings, ListViewOptions &opts) {
  opts.listFontSize =
      settings.value(keys::kListFontSize, UIConstants::Item::DEFAULT_FONT_SIZE).toInt();
  opts.listRowHeight =
      settings.value(keys::kListRowHeight, UIConstants::ListView::DEFAULT_ROW_HEIGHT).toInt();
  opts.listRowColor = settings.value(keys::kListRowColor).toString();
  opts.listAltRowColor = settings.value(keys::kListAltRowColor).toString();
}

void save(QSettings &settings, const ListViewOptions &opts) {
  settings.setValue(keys::kListFontSize, opts.listFontSize);
  settings.setValue(keys::kListRowHeight, opts.listRowHeight);
  settings.setValue(keys::kListRowColor, opts.listRowColor);
  settings.setValue(keys::kListAltRowColor, opts.listAltRowColor);
}

} // namespace ListViewOptionsPersistence
