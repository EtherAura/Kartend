#ifndef KARTEND_UTILS_APP_COLLECTION_KEYBINDING_SETTINGS_H
#define KARTEND_UTILS_APP_COLLECTION_KEYBINDING_SETTINGS_H

// Leaf struct peeled out of GeneralSettings (Kartend-q1w6). Single-key
// keyboard bindings (no modifier semantics); defaults match the original
// hard-coded behavior. Stored as Qt::Key integer values.

#include <QtCore/Qt>

struct KeybindingSettings {
  int keyNavLeft = Qt::Key_Left;
  int keyNavRight = Qt::Key_Right;
  int keyNavUp = Qt::Key_Up;
  int keyNavDown = Qt::Key_Down;
  int keyConfirm = Qt::Key_Return; // Return/Enter treated as equivalent
  int keyBack = Qt::Key_Escape;
  int keySearch = Qt::Key_Slash;
  int keyAlphabeticBack = Qt::Key_PageUp;
  int keyAlphabeticForward = Qt::Key_PageDown;
  int keyJumpFirst = Qt::Key_Home;
  int keyJumpLast = Qt::Key_End;
  // Opens the dedicated item-detail page for the current selection. Ignored
  // while the search bar has focus so typing "i" in the filter still works.
  int keyItemDetails = Qt::Key_I;
  // Jumps directly to the synthetic Home view from any nesting depth. Default
  // 0 = unbound so an upgrading install picks up no surprise shortcut; only
  // honored when useHomeView is enabled.
  int keyHomeView = 0;
  // Toggles the collection tree panel (Kartend-ob1c9). Default 0 = unbound —
  // F6 via the View-menu action is the out-of-the-box binding; this is the
  // user's rebindable alternative, keyHomeView-style.
  int keyToggleCollectionTree = 0;
  // Defaulted memberwise equality — keeps GeneralSettings::operator== and the
  // settings dirty-check field-complete automatically (Kartend-6oqat).
  bool operator==(const KeybindingSettings &) const = default;
};

#endif // KARTEND_UTILS_APP_COLLECTION_KEYBINDING_SETTINGS_H
