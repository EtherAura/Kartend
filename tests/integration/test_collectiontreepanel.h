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
  // Toggling the tree off shows the marker; clicking the marker restores
  // the tree and hides the marker again.
  void foldMarker_appearsWhenTreeHidden_andClickRestores();

  // Every branch opens EXPANDED on a fresh session (collapsed-set memory,
  // user decision 2026-08-17) — and childless rows never poison the
  // collapse memory during staged startup rebuilds.
  void tree_opensFullyExpanded_onFreshSession();

  // Pixel test: renders the real tree with a seeded icon on an INDENTED row
  // and measures where it lands — centred in the row's visible span, fully
  // inside the viewport. Guards the decoration-rect geometry that clipped
  // icons at the panel edge (field reports 2026-08-17).
  void icons_onIndentedRows_renderCenteredAndUnclipped();

  // Select+direction section chord (2026-08-17): left focuses the tree,
  // right returns to the grid; a hidden tree is skipped.
  void focusSectionChord_movesBetweenTreeAndGrid();
};

#endif // TEST_COLLECTIONTREEPANEL_H
