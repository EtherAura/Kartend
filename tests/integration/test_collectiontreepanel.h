#ifndef TEST_COLLECTIONTREEPANEL_H
#define TEST_COLLECTIONTREEPANEL_H

#include <QObject>

// Integration tests for the collection tree panel's fold marker: a hidden
// tree must leave a visible, clickable way back at its dock edge (user
// request 2026-08-17). Runs against the real MainWindow wiring so the
// marker's layout insertion, visibility sync, and click path are all the
// production ones.
class TestCollectionTreePanel : public QObject {
  Q_OBJECT
private slots:
  // A hidden tree leaves NO on-screen affordance (user decision
  // 2026-08-18): it comes back via the shortcut, the gamepad, or the View
  // menu — never a floating tab.
  void hiddenTree_leavesNoIndicatorBehind();

  // Every branch opens EXPANDED on a fresh session (collapsed-set memory,
  // user decision 2026-08-17) — and childless rows never poison the
  // collapse memory during staged startup rebuilds.
  void tree_opensFullyExpanded_onFreshSession();

  // Pixel test: renders the real tree with a seeded icon on an INDENTED row
  // and measures where it lands — centred in the row's visible span, fully
  // inside the viewport. Guards the decoration-rect geometry that clipped
  // icons at the panel edge (field reports 2026-08-17).
  void icons_onIndentedRows_renderCenteredAtConfiguredSize();
  void iconAndText_drawsTheNameBesideTheIcon();

  // Kartend-1kkk2: the RetroArch system glyph is an option set of its own, so
  // it must render in the DEFAULT Name-only mode and with no collectionIcon
  // set — i.e. without borrowing anything from the row-artwork pipeline. That
  // independence is the whole design decision, so it is what gets pinned.
  void systemIcon_drawsInNameOnlyModeAtTheConfiguredSize();

  // Kartend-1kkk2, field report 2026-08-22 ("still shows controller icon
  // after choosing console"): editing ONLY the glyph's settings must rebake
  // the rows. The rebake test enumerated the tree's own icon prefs and not
  // this separate cluster, so the setting saved and the screen did not move.
  void systemIcon_settingsChangeRebakesTheRow();

  // Select+direction section chord (2026-08-17): left focuses the tree,
  // right returns to the grid; a hidden tree is skipped.
  void focusSectionChord_movesBetweenTreeAndGrid();

  // Modifier HUD (2026-08-18): holding the gamepad modifier shows the
  // desaturating overlay with a section indicator; releasing hides it.
  void focusModifierHud_showsWhileHeldAndHidesOnRelease();

  // Right-stick routing (2026-08-18): up from the grid lands on the
  // toolbar, and once a section owns focus the stick drives that section's
  // list instead of switching sections.
  void rightStick_upFocusesToolbar_andDrivesTheFocusedSection();

  // 2026-08-18: vertical reaches the toolbar ONLY with the modifier held;
  // unheld it drives the details pane, and the pulsing ring marks what is
  // selected.
  void rightStick_upFromGridFocusesToolbar_paneWalkKeepsVertical();

  // 2026-08-18: traversing collections from the tree must not hand focus
  // back to the window, or the stick stops driving the tree mid-traversal.
  void treeTraversal_keepsTreeFocusedAcrossCollectionSwitch();

  // 2026-08-18: expand mode owns the gamepad — Back dismisses the artwork
  // instead of leaving the collection behind it.
  void expandedArtwork_backDismissesInsteadOfLeavingCollection();

  // 2026-08-18: a second of no stick input hands focus back to the grid
  // and drops the pane ring.
  void paneSelection_idleReturnsFocusToGrid();

  // 2026-08-18: cycling artwork stops at the ends and reports a boundary
  // (the interaction layer steps to the neighbouring item) instead of
  // wrapping back to the same item's first picture.
  void expandedArtwork_cyclesThenReportsBoundaryInsteadOfWrapping();

  // 2026-08-18: the boundary hand-off must be connected however expand
  // mode was opened — keyboard and wheel reach the overlay directly and
  // never ask the interaction layer for anything.
  void expandedArtwork_boundaryIsHookedRegardlessOfInput();

  // 2026-08-18: one artwork per wheel gesture, however hard the scroll.
  void expandedArtwork_wheelAdvancesOneArtworkPerGesture();

  // 2026-08-18: an item with no artwork shows the grid's hatched
  // placeholder instead of leaving the previous item's picture up, and the
  // wheel never leaks to the grid behind the overlay.
  void expandedArtwork_artlessItemShowsPlaceholder_andWheelNeverLeaks();

  // 2026-08-18: a kinetic wheel streams events for as long as it coasts.
  // The gallery must advance ONCE for the whole stream, not once per
  // cooldown period — that steady march was the filmed runaway.
  void expandedArtwork_kineticWheelStreamAdvancesOnlyOnce();

  // 2026-08-18: stepping items programmatically must not leave the
  // keyboard's "key is held" flag armed — that phantom hold is what kept
  // the grid scrolling after a traversal.
  void expandedArtwork_itemStepLeavesNoPhantomKeyHeld();

  // 2026-08-18 styling contract, measured rather than asserted by eye:
  // overlay scrollbars really replace the native ones on the tree, the
  // first row matches the toolbar's height, and the chrome is one colour.
  void navPanel_matchesToolbarHeightAndTone_andHidesNativeScrollbar();
};

#endif // TEST_COLLECTIONTREEPANEL_H
