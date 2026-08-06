// Kartend-q40q0: WCAG contrast arithmetic + the lightness-repair helper that
// fixes the bbcu6 class of bug (palette hue + absolute saturation/lightness
// pinning contrast against one background polarity). The three repair cases
// below are the audit's real findings, with their measured shipped ratios:
// the breadcrumb link (1.95:1 on Breeze Light), the opt-in title tint
// (1.85:1 on Breeze Dark) and the validation-error red (3.72:1 / 3.22:1).
#include "colorcontrast.h"

#include <QColor>
#include <QTest>

namespace {

// Breeze window backgrounds — the pair every text-bearing derivation must
// survive (Kartend-q40q0 acceptance criteria).
const QColor kBreezeLightWindow("#eff0f1");
const QColor kBreezeDarkWindow("#2a2e32");

} // namespace

class TestColorContrast : public QObject {
  Q_OBJECT
private slots:
  void relativeLuminance_anchorsBlackAndWhite();
  void contrastRatio_matchesKnownWcagValues();
  void ensureContrast_compliantColorPassesThroughUnchanged();
  void ensureContrast_repairsAuditFindingsOnBothThemes();
  void ensureContrast_preservesHueSaturationAlpha();
  void ensureContrast_unreachableTargetReturnsPole();
};

void TestColorContrast::relativeLuminance_anchorsBlackAndWhite() {
  QCOMPARE(ColorContrast::relativeLuminance(QColor(Qt::black)), 0.0);
  QCOMPARE(ColorContrast::relativeLuminance(QColor(Qt::white)), 1.0);
  // Monotone in lightness — the property ensureContrast's scan relies on.
  QVERIFY(ColorContrast::relativeLuminance(QColor::fromHsl(200, 180, 60)) <
          ColorContrast::relativeLuminance(QColor::fromHsl(200, 180, 200)));
}

void TestColorContrast::contrastRatio_matchesKnownWcagValues() {
  // Black on white is the 21:1 ceiling; identical colours the 1:1 floor.
  QVERIFY(qAbs(ColorContrast::contrastRatio(QColor(Qt::black), QColor(Qt::white)) - 21.0) < 0.01);
  QVERIFY(qAbs(ColorContrast::contrastRatio(kBreezeDarkWindow, kBreezeDarkWindow) - 1.0) < 0.001);
  // Symmetric in its arguments.
  QCOMPARE(ColorContrast::contrastRatio(QColor("#d05050"), kBreezeLightWindow),
           ColorContrast::contrastRatio(kBreezeLightWindow, QColor("#d05050")));
  // bbcu6's measured headline number: the old shipped title tint against
  // Breeze Dark's grid background came out at 1.63:1.
  const double bbcu6 = ColorContrast::contrastRatio(QColor(18, 73, 102), QColor("#202326"));
  QVERIFY(qAbs(bbcu6 - 1.63) < 0.01);
}

void TestColorContrast::ensureContrast_compliantColorPassesThroughUnchanged() {
  // The breadcrumb tint on a dark window already measures 6.15:1 — the
  // repair must not touch it (rgb() compare: identical channels, not just
  // ratio-compliant).
  const QColor link = QColor::fromHsl(201, 101, 170);
  QCOMPARE(ColorContrast::ensureContrast(link, kBreezeDarkWindow).rgba(), link.rgba());
}

void TestColorContrast::ensureContrast_repairsAuditFindingsOnBothThemes() {
  const struct {
    QColor fg;
    QColor bg;
  } findings[] = {
      // Breadcrumb link (navigationmanagertitle): absolute L=170 on light.
      {QColor::fromHsl(201, 101, 170), kBreezeLightWindow},
      // Opt-in title tint defaults (S=180, L=75) on dark.
      {QColor::fromHsl(201, 180, 75), kBreezeDarkWindow},
      // Validation-error red on both polarities.
      {QColor("#d05050"), kBreezeLightWindow},
      {QColor("#d05050"), kBreezeDarkWindow},
  };
  for (const auto &f : findings) {
    const QColor repaired = ColorContrast::ensureContrast(f.fg, f.bg);
    QVERIFY2(ColorContrast::contrastRatio(repaired, f.bg) >= ColorContrast::kAaNormalText,
             qPrintable(QStringLiteral("%1 on %2 repaired to %3 at %4:1")
                            .arg(f.fg.name(), f.bg.name(), repaired.name())
                            .arg(ColorContrast::contrastRatio(repaired, f.bg))));
  }
}

void TestColorContrast::ensureContrast_preservesHueSaturationAlpha() {
  const QColor tint = QColor::fromHsl(201, 180, 75, 200);
  const QColor repaired = ColorContrast::ensureContrast(tint, kBreezeDarkWindow);
  int th = 0, ts = 0, tl = 0, ta = 0;
  int rh = 0, rs = 0, rl = 0, ra = 0;
  tint.getHsl(&th, &ts, &tl, &ta);
  repaired.getHsl(&rh, &rs, &rl, &ra);
  QCOMPARE(rh, th);
  QCOMPARE(rs, ts);
  QCOMPARE(ra, ta);
  // Dark background → the repair must have lightened, minimally past AA,
  // not slammed to the white pole.
  QVERIFY(rl > tl);
  QVERIFY(rl < 255);
}

void TestColorContrast::ensureContrast_unreachableTargetReturnsPole() {
  // Mid-grey background: neither pole reaches 21:1, so the scan runs out and
  // returns the pole — the maximum achievable, never an infinite loop.
  const QColor midGrey("#808080");
  const QColor repaired = ColorContrast::ensureContrast(QColor("#909090"), midGrey, 21.0);
  int h = 0, s = 0, l = 0, a = 0;
  repaired.getHsl(&h, &s, &l, &a);
  QVERIFY(l == 0 || l == 255);
}

QTEST_MAIN(TestColorContrast)
#include "test_colorcontrast.moc"
