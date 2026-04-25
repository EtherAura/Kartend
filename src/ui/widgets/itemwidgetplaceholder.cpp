// Placeholder pattern builder extracted from itemwidget.cpp.
// This remains an ItemWidget member; this is a translation-unit split.
#include "itemwidget.h"
#include "uiconstants.h"
#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRandomGenerator>
#include <QRectF>
#include <Qt>

// Build placeholder pattern
auto ItemWidget::buildPlaceholderPattern(int width, int height) const -> QPixmap {
  if (width <= 0 || height <= 0) {
    return {};
  }
  static QPixmap cache;
  static quint64 cacheKey = 0;
  static int cachedCornerRadius = 0;
  static QString cachedTileColor;
  QColor base = palette().color(QPalette::Mid);
  constexpr int kKeyWidthShiftBits = 48;
  constexpr int kKeyHeightShiftBits = 32;
  constexpr quint64 kRgbaMask32 = 0xffffffffULL;
  quint64 key = (static_cast<quint64>(width) << kKeyWidthShiftBits) |
                (static_cast<quint64>(height) << kKeyHeightShiftBits) |
                (static_cast<quint64>(base.rgba()) & kRgbaMask32);
  if (!cache.isNull() && cacheKey == key && cachedCornerRadius == m_cornerRadius &&
      cachedTileColor == s_tileColor) {
    return cache;
  }

  QPixmap pixmap(width, height);
  pixmap.fill(Qt::transparent);

  int baseLightness = base.lightness();
  constexpr int kLightnessDarkThreshold = 128;
  bool dark = (baseLightness < kLightnessDarkThreshold);

  int primaryDelta = dark ? UIConstants::Placeholder::PRIMARY_DELTA_DARK
                          : UIConstants::Placeholder::PRIMARY_DELTA_LIGHT;
  int secondaryDelta = dark ? UIConstants::Placeholder::SECONDARY_DELTA_DARK
                            : UIConstants::Placeholder::SECONDARY_DELTA_LIGHT;

  int hHue = 0;
  int hSat = 0;
  int hLight = 0;
  int hAlpha = 0;
  QColor primary = base;
  primary.getHsl(&hHue, &hSat, &hLight, &hAlpha);
  primary.setHsl(hHue, hSat / 2, qBound(0, hLight + primaryDelta, UIConstants::Color::CHANNEL_MAX),
                 UIConstants::Color::CHANNEL_MAX);
  // Use per-collection tile color if set, otherwise use title tint
  QColor accentColor;
  if (!s_tileColor.isEmpty() && QColor::isValidColorName(s_tileColor)) {
    accentColor = QColor(s_tileColor);
  } else {
    accentColor = titleTint();
  }
  primary.setRed((primary.red() * UIConstants::Placeholder::PRIMARY_TINT_NUM +
                  accentColor.red() * (UIConstants::Placeholder::PRIMARY_TINT_DEN -
                                       UIConstants::Placeholder::PRIMARY_TINT_NUM)) /
                 UIConstants::Placeholder::PRIMARY_TINT_DEN);
  primary.setGreen((primary.green() * UIConstants::Placeholder::PRIMARY_TINT_NUM +
                    accentColor.green() * (UIConstants::Placeholder::PRIMARY_TINT_DEN -
                                           UIConstants::Placeholder::PRIMARY_TINT_NUM)) /
                   UIConstants::Placeholder::PRIMARY_TINT_DEN);
  primary.setBlue((primary.blue() * UIConstants::Placeholder::PRIMARY_TINT_NUM +
                   accentColor.blue() * (UIConstants::Placeholder::PRIMARY_TINT_DEN -
                                         UIConstants::Placeholder::PRIMARY_TINT_NUM)) /
                  UIConstants::Placeholder::PRIMARY_TINT_DEN);
  primary.setAlpha(UIConstants::Placeholder::PRIMARY_ALPHA);

  QColor secondary = base;
  secondary.setHsl(hHue, hSat / 3,
                   qBound(0, hLight + secondaryDelta, UIConstants::Color::CHANNEL_MAX),
                   UIConstants::Color::CHANNEL_MAX);
  secondary.setRed((secondary.red() * UIConstants::Placeholder::SECONDARY_TINT_NUM +
                    accentColor.red() * (UIConstants::Placeholder::SECONDARY_TINT_DEN -
                                         UIConstants::Placeholder::SECONDARY_TINT_NUM)) /
                   UIConstants::Placeholder::SECONDARY_TINT_DEN);
  secondary.setGreen((secondary.green() * UIConstants::Placeholder::SECONDARY_TINT_NUM +
                      accentColor.green() * (UIConstants::Placeholder::SECONDARY_TINT_DEN -
                                             UIConstants::Placeholder::SECONDARY_TINT_NUM)) /
                     UIConstants::Placeholder::SECONDARY_TINT_DEN);
  secondary.setBlue((secondary.blue() * UIConstants::Placeholder::SECONDARY_TINT_NUM +
                     accentColor.blue() * (UIConstants::Placeholder::SECONDARY_TINT_DEN -
                                           UIConstants::Placeholder::SECONDARY_TINT_NUM)) /
                    UIConstants::Placeholder::SECONDARY_TINT_DEN);
  secondary.setAlpha(UIConstants::Placeholder::SECONDARY_ALPHA);

  int step = qBound(UIConstants::Placeholder::STEP_MIN,
                    qMin(width, height) / UIConstants::Placeholder::STEP_DIVISOR,
                    UIConstants::Placeholder::STEP_MAX);

  // Determine background color - use tileColor if set, otherwise use palette
  QColor bgColor = base;
  if (!s_tileColor.isEmpty() && QColor::isValidColorName(s_tileColor)) {
    bgColor = QColor(s_tileColor);
    // Note: dark was originally recalculated here for tileColor lightness,
    // but primaryDelta/secondaryDelta are already computed above using the
    // original dark value based on base color, which is the intended behavior
  }

  {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // Fill background with tileColor or palette base
    painter.fillRect(0, 0, width, height, bgColor);

    painter.setPen(QPen(primary, 1));
    for (int diag = -height; diag < width; diag += step) {
      painter.drawLine(diag, 0, diag + height, height);
    }
    painter.setPen(QPen(secondary, 1));
    for (int diag = -height; diag < width; diag += step * 2) {
      painter.drawLine(diag, height, diag + height, 0);
    }
  }

  QImage img = pixmap.toImage();
  QRandomGenerator generator(static_cast<quint32>(key ^ UIConstants::Placeholder::NOISE_SEED));
  const int noiseAmp = UIConstants::Placeholder::NOISE_AMPLITUDE;
  const int stride = UIConstants::Placeholder::NOISE_STRIDE;
  if (noiseAmp > 0 && stride > 0) {
    for (int yPos = 0; yPos < height; yPos += stride) {
      QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(yPos));
      for (int xPos = 0; xPos < width; xPos += stride) {
        QRgb pixel = row[xPos];
        int red = qRed(pixel);
        int green = qGreen(pixel);
        int blue = qBlue(pixel);
        int noiseDelta =
            static_cast<int>(generator.generate() & UIConstants::Placeholder::NOISE_MASK) -
            UIConstants::Placeholder::NOISE_BIAS;
        noiseDelta = std::min(noiseDelta, noiseAmp);
        noiseDelta = std::max(noiseDelta, -noiseAmp);
        red = qBound(0, red + noiseDelta, UIConstants::Color::CHANNEL_MAX);
        green = qBound(0, green + noiseDelta, UIConstants::Color::CHANNEL_MAX);
        blue = qBound(0, blue + noiseDelta, UIConstants::Color::CHANNEL_MAX);
        row[xPos] = qRgb(red, green, blue);
      }
    }
  }
  pixmap = QPixmap::fromImage(img);

  {
    QPainter painter(&pixmap);
    QLinearGradient gradient(0, 0, 0, height);
    QColor top(bgColor.red(), bgColor.green(), bgColor.blue(),
               UIConstants::Placeholder::GRADIENT_TOP_ALPHA);
    QColor bottom(bgColor.red(), bgColor.green(), bgColor.blue(),
                  UIConstants::Placeholder::GRADIENT_BOTTOM_ALPHA);
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);
    painter.fillRect(0, 0, width, height, gradient);
  }

  // Apply corner radius masking at the end (after all drawing/processing)
  if (m_cornerRadius > 0) {
    QPixmap maskedPixmap(width, height);
    maskedPixmap.fill(Qt::transparent);

    QPainter maskPainter(&maskedPixmap);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(0, 0, width, height), m_cornerRadius, m_cornerRadius);
    maskPainter.setClipPath(clipPath);
    maskPainter.drawPixmap(0, 0, pixmap);
    maskPainter.end();

    pixmap = maskedPixmap;
  }

  cache = pixmap;
  cacheKey = key;
  cachedCornerRadius = m_cornerRadius;
  cachedTileColor = s_tileColor;
  return pixmap;
}
