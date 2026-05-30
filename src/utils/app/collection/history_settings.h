#ifndef KARTEND_UTILS_APP_COLLECTION_HISTORY_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_HISTORY_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Launch-history
// toggle + soft cap. Disabling stops new-row inserts and hides the dialog
// tab; existing rows stay viewable until cleared. historyMaxEntries is
// applied after every insert; clamped at load/save.

struct HistorySettings {
  bool historyEnabled = true;
  int historyMaxEntries = 500;
};

#endif // KARTEND_UTILS_APP_COLLECTION_HISTORY_SETTINGS_H
