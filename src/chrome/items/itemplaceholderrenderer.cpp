// Pure rendering helpers for the "missing artwork" placeholder tile that
// item cards and the sidebar's pattern background both share. Lifted out of
// itemwidgetplaceholder.cpp so the rendering and its cache live in a
// cohesive namespace; ItemWidget's existing static API forwards here.
#include "itemplaceholderrenderer.h"

#include "uiconstants/color.h"
#include "uiconstants/placeholder.h"

#include <algorithm>
#include <QApplication>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QRandomGenerator>
#include <QRect>
#include <QRectF>
#include <QString>

namespace ItemPlaceholderRenderer {

QImage buildTileImage(int width, int height, int cornerRadius, bool applyGradient,
                      const QColor &base, double lineAlphaScale, const TileTheme &theme,
                      quint64 noiseSeed) {
  if (width <= 0 || height <= 0) {
    return {};
  }

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
  // Use per-collection tile color if set, otherwise use the title tint
  // accent the caller resolved.
  const QColor accentColor = theme.tileColor.isValid() ? theme.tileColor : theme.accentColor;
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
  primary.setAlpha(static_cast<int>(
      std::clamp(UIConstants::Placeholder::PRIMARY_ALPHA * lineAlphaScale, 0.0, 255.0)));

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
  secondary.setAlpha(static_cast<int>(
      std::clamp(UIConstants::Placeholder::SECONDARY_ALPHA * lineAlphaScale, 0.0, 255.0)));

  int step = qBound(UIConstants::Placeholder::STEP_MIN,
                    qMin(width, height) / UIConstants::Placeholder::STEP_DIVISOR,
                    UIConstants::Placeholder::STEP_MAX);

  // Determine background color — use tileColor if set, otherwise use
  // the palette base. primaryDelta / secondaryDelta were already computed
  // above using the original `base` lightness, which is the intended
  // behavior even when tileColor is overriding the fill.
  const QColor bgColor = theme.tileColor.isValid() ? theme.tileColor : base;

  QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  {
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);
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

  QRandomGenerator generator(
      static_cast<quint32>(noiseSeed ^ UIConstants::Placeholder::NOISE_SEED));
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

  if (applyGradient) {
    QPainter painter(&img);
    QLinearGradient gradient(0, 0, 0, height);
    QColor top(bgColor.red(), bgColor.green(), bgColor.blue(),
               UIConstants::Placeholder::GRADIENT_TOP_ALPHA);
    QColor bottom(bgColor.red(), bgColor.green(), bgColor.blue(),
                  UIConstants::Placeholder::GRADIENT_BOTTOM_ALPHA);
    gradient.setColorAt(0.0, top);
    gradient.setColorAt(1.0, bottom);
    painter.fillRect(0, 0, width, height, gradient);
  }

  if (cornerRadius > 0) {
    QImage masked(width, height, QImage::Format_ARGB32_Premultiplied);
    masked.fill(Qt::transparent);
    QPainter maskPainter(&masked);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(0, 0, width, height), cornerRadius, cornerRadius);
    maskPainter.setClipPath(clipPath);
    maskPainter.drawImage(0, 0, img);
    maskPainter.end();
    img = masked;
  }

  return img;
}

QPixmap buildTile(int width, int height, int cornerRadius, bool applyGradient,
                  const QColor &baseOverride, double lineAlphaScale, const TileTheme &theme) {
  if (width <= 0 || height <= 0) {
    return {};
  }
  // Per-size cache: avoids thrash when the sidebar (different size) and item
  // tiles call this helper in alternation. The cached-theme guard wipes the
  // cache whenever the user picks a new tile color so a stale tile doesn't leak
  // into the next render.
  static QHash<quint64, QPixmap> cache;
  static QString cachedTileColorKey;
  // Default to the global app palette Mid; callers (e.g. DetailsPane) can pass
  // their own widget-instance Mid so an inherited custom palette is honoured.
  const QColor base =
      baseOverride.isValid() ? baseOverride : QApplication::palette().color(QPalette::Mid);
  constexpr int kKeyWidthShiftBits = 48;
  constexpr int kKeyHeightShiftBits = 32;
  constexpr quint64 kRgbaMask32 = 0xffffffffULL;
  quint64 key = (static_cast<quint64>(width) << kKeyWidthShiftBits) |
                (static_cast<quint64>(height) << kKeyHeightShiftBits) |
                (static_cast<quint64>(base.rgba()) & kRgbaMask32);
  // Mix corner radius + the gradient flag + the quantized alpha scale into the
  // key so otherwise same-size variants get their own cache slots.
  key ^= static_cast<quint64>(cornerRadius) << 16;
  if (applyGradient) {
    key ^= 0x1ULL;
  }
  key ^= static_cast<quint64>(std::clamp(lineAlphaScale * 100.0, 0.0, 200.0)) << 24;
  const QString themeKey = theme.tileColor.isValid() ? theme.tileColor.name() : QString();
  if (cachedTileColorKey != themeKey) {
    cache.clear();
    cachedTileColorKey = themeKey;
  }
  auto cacheIt = cache.constFind(key);
  if (cacheIt != cache.constEnd()) {
    return cacheIt.value();
  }

  // Render via the shared QImage core (seed = cache key, so the dither stays
  // stable per cache slot — identical output to the previous inline path),
  // then wrap in a QPixmap and cache.
  const QPixmap pixmap = QPixmap::fromImage(
      buildTileImage(width, height, cornerRadius, applyGradient, base, lineAlphaScale, theme, key));
  cache.insert(key, pixmap);
  return pixmap;
}

void drawTitle(QPixmap &pixmap, const QString &itemName, const QFont &baseFont,
               const TitleStyle &style, qreal dpr) {
  if (pixmap.isNull() || itemName.isEmpty()) {
    return;
  }
  const int width = pixmap.width();
  const int height = pixmap.height();
  if (width <= 0 || height <= 0) {
    return;
  }

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.setRenderHint(QPainter::Antialiasing);

  QFont titleFont = baseFont;
  if (!style.customFontFamily.isEmpty()) {
    titleFont.setFamily(style.customFontFamily);
  }
  // Scale point size for physical-pixel pixmaps so the overlay matches
  // the visual weight of the on-screen nameLabel regardless of DPR.
  if (titleFont.pointSizeF() > 0) {
    titleFont.setPointSizeF(titleFont.pointSizeF() * dpr);
  } else if (titleFont.pixelSize() > 0) {
    titleFont.setPixelSize(qRound(titleFont.pixelSize() * dpr));
  }
  painter.setFont(titleFont);

  const int margin = qMax(4, qMin(width, height) / 16);
  QRect textRect(margin, margin, width - 2 * margin, height - 2 * margin);
  const int flags = Qt::AlignCenter | Qt::TextWordWrap;

  // Translucent dark wash sized to the wrapped title — keeps the text
  // legible over both the hatched pattern and any user-supplied
  // placeholder image without obscuring the whole tile.
  QFontMetrics fm(titleFont);
  QRect bounds = fm.boundingRect(textRect, flags, itemName);
  if (bounds.isValid()) {
    const int pad = qMax(2, margin / 2);
    QRect washRect = bounds.adjusted(-pad, -pad, pad, pad).intersected(pixmap.rect());
    QColor wash(0, 0, 0, 140);
    painter.fillRect(washRect, wash);
  }

  painter.setPen(style.tintColor);
  painter.drawText(textRect, flags, itemName);
}

} // namespace ItemPlaceholderRenderer
