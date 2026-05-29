#ifndef TEST_SETTINGSDIALOG_PERF_H
#define TEST_SETTINGSDIALOG_PERF_H

#include <QObject>

// Performance-regression guard for SettingsDialog construction (Kartend-cnnq).
// Builds the synthetic large-config stress profile the bd's Step 1 specified
// (50 collections x 500 custom artwork types) and asserts the dialog still
// opens within a generous budget — catching a future config-dependent stall
// that the original (real-config) profiling could not exercise.
class TestSettingsDialogPerf : public QObject {
  Q_OBJECT
private slots:
  void largeConfigOpensWithinBudget();
};

#endif // TEST_SETTINGSDIALOG_PERF_H
