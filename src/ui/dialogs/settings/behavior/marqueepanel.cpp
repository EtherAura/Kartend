#include "marqueepanel.h"

#include "collection/generalsettings.h"
#include "settingsformbinding.h"
#include "settingsmodel.h"
#include "ui_marqueepanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalBlocker>

namespace {

// Friendly label for the screen combo. QScreen::name() alone is opaque
// on multi-output GPUs ("HDMI-A-1", "DP-1"); enriching it with the
// resolution helps the user pick the right physical monitor.
QString labelForScreen(QScreen *screen) {
  if (!screen) return {};
  const QRect g = screen->geometry();
  return QStringLiteral("%1  —  %2×%3").arg(screen->name()).arg(g.width()).arg(g.height());
}

} // namespace

MarqueePanel::MarqueePanel(QWidget *parent) : QWidget(parent), ui(new Ui::MarqueePanel) {
  ui->setupUi(this);

  // Mode combo — order matches the int values stored in
  // GeneralSettings::marqueeMode so the index doubles as the persisted
  // value (no separate lookup table needed). Mode 2 (video / attract
  // loop) plays the item's preview video — falling back to the
  // collection's backgroundVideo when the item has none.
  ui->marqueeModeComboBox->addItem(tr("Item Artwork (follows selection)"));
  ui->marqueeModeComboBox->addItem(tr("Collection Icon"));
  ui->marqueeModeComboBox->addItem(tr("Video / Attract Loop"));

  populateScreens();

  connect(ui->marqueeEnabledCheckBox, &QCheckBox::toggled, this, [this](bool) { save(); });
  connect(ui->marqueeScreenComboBox, &QComboBox::currentIndexChanged, this,
          [this](int) { save(); });
  connect(ui->marqueeModeComboBox, &QComboBox::currentIndexChanged, this, [this](int) { save(); });
}

MarqueePanel::~MarqueePanel() {
  delete ui;
}

void MarqueePanel::setModel(SettingsModel *model) {
  m_model = model;
  load();
}

void MarqueePanel::populateScreens() {
  // Combo carries the screen's QScreen::name() as item data so the
  // saved value survives display reorderings (index would shift if
  // the user replugged in a different order).
  QSignalBlocker blocker(ui->marqueeScreenComboBox);
  ui->marqueeScreenComboBox->clear();
  ui->marqueeScreenComboBox->addItem(tr("(Primary screen)"), QString());
  for (QScreen *s : QGuiApplication::screens()) {
    ui->marqueeScreenComboBox->addItem(labelForScreen(s), s->name());
  }
}

void MarqueePanel::load() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  SettingsFormBinding::loadInto(ui->marqueeEnabledCheckBox,
                                m_model->generalSettings->marquee.marqueeEnabled);
  {
    QSignalBlocker blocker(ui->marqueeScreenComboBox);
    const QString saved = m_model->generalSettings->marquee.marqueeScreenName;
    int idx = ui->marqueeScreenComboBox->findData(saved);
    if (idx < 0) {
      // The saved screen is no longer present; surface that to the
      // user by leaving the combo on "Primary" but tag the tooltip so
      // they know their old pick was dropped.
      idx = 0;
      ui->marqueeScreenComboBox->setToolTip(
          tr("Saved screen '%1' isn't connected — falling back to the primary screen.").arg(saved));
    } else {
      ui->marqueeScreenComboBox->setToolTip(QString());
    }
    ui->marqueeScreenComboBox->setCurrentIndex(idx);
  }
  {
    QSignalBlocker blocker(ui->marqueeModeComboBox);
    ui->marqueeModeComboBox->setCurrentIndex(qBound(
        0, m_model->generalSettings->marquee.marqueeMode, ui->marqueeModeComboBox->count() - 1));
  }
}

void MarqueePanel::save() {
  if (!m_model || !m_model->generalSettings) {
    return;
  }
  m_model->generalSettings->marquee.marqueeEnabled = ui->marqueeEnabledCheckBox->isChecked();
  m_model->generalSettings->marquee.marqueeScreenName =
      ui->marqueeScreenComboBox->currentData().toString();
  m_model->generalSettings->marquee.marqueeMode = ui->marqueeModeComboBox->currentIndex();
  emit changed();
}
