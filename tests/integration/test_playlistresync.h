/**
 * @file test_playlistresync.h
 * @brief Regression coverage for MainWindow::resyncPlaylistCollections.
 */

#ifndef KARTEND_TESTS_TEST_PLAYLISTRESYNC_H
#define KARTEND_TESTS_TEST_PLAYLISTRESYNC_H

#include <QObject>

class TestPlaylistResync : public QObject {
  Q_OBJECT

private slots:
  void reentrantPlaylistsChanged_doesNotDuplicateFavorites();
};

#endif // KARTEND_TESTS_TEST_PLAYLISTRESYNC_H
