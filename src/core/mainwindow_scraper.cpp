// MainWindow's marquee-shim entry points + IMainWindow forwarders to the
// extracted ScraperController (Kartend-hzef step 3).
//
// The scraper-flow methods (openScraperDialog + promptResumePendingScrapeIfAny)
// and the pickLookupProvider helper moved to scrapercontroller.{h,cpp}. The
// IMainWindow virtual stays here so callers reaching MainWindow through the
// interface (interaction context menu, menu lambda) don't need to know about
// the controller; it just forwards.
//
// The marquee forwarders are too small to need their own TU — they stay
// because MarqueeController is owned by MainWindow and these are the only
// other entry points in this file's surface area.

#include "applicationmanager.h"
#include "mainwindow.h"
#include "marqueecontroller.h"
#include "scrapercontroller.h"

void MainWindow::applyMarqueeSettings() {
  if (m_marqueeController) {
    m_marqueeController->applyMarqueeSettings();
  }
}

void MainWindow::updateMarqueeArtwork() {
  if (m_marqueeController) {
    m_marqueeController->updateMarqueeArtwork();
  }
}

void MainWindow::openScraperDialog(int preCollectionIndex, const QString &preItemPath) {
  if (m_scraperController) {
    m_scraperController->openScraperDialog(preCollectionIndex, preItemPath);
  }
}

void MainWindow::promptResumePendingScrapeIfAny() {
  if (m_scraperController) {
    m_scraperController->promptResumePendingScrapeIfAny();
  }
}
