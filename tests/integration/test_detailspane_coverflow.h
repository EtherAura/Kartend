#ifndef TEST_DETAILSPANE_COVERFLOW_H
#define TEST_DETAILSPANE_COVERFLOW_H

#include <QObject>

// Integration tests for DetailsPane static formatters and CoverFlowWidget
// public surface. The two largest UI files in the repo had
// zero direct test coverage before this; these add behavioral tests for
// the parts that are reachable without a fully-wired collection.
class TestDetailsPaneCoverflow : public QObject {
  Q_OBJECT
private slots:
  // DetailsPane pure formatters
  void formatRuntime_negativeReturnsEmpty();
  void formatRuntime_secondsOnlyBranch();
  void formatRuntime_minutesAndSeconds();
  void formatRuntime_hoursAndMinutes();

  void formatFileSize_smallReportsBytes();
  void formatFileSize_kiloRange();
  void formatFileSize_megaRange();
  void formatFileSize_gigaRange();

  void formatTags_jsonArrayStripsBrackets();
  void formatTags_commaSeparatedTrimsAndJoins();
  void formatTags_emptyStaysEmpty();
  void formatTags_skipsEmptyParts();

  void formatLastScanned_invalidReturnsNeverLabel();
  void formatLastScanned_validRendersIso();

  // CoverFlowWidget public surface
  void coverflow_emptyAtConstructionThenSetCardsCounts();
  void coverflow_setSelectedIndexClampsToValidRange();
  void coverflow_selectionChangeFromUserEmitsRequest();
  void coverflow_setCornerRadiusAndColorsAreNoCrash();
  void coverflow_setCardsClearsAndReplaces();
};

#endif // TEST_DETAILSPANE_COVERFLOW_H
