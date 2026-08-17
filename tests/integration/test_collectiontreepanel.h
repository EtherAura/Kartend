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
};

#endif // TEST_COLLECTIONTREEPANEL_H
