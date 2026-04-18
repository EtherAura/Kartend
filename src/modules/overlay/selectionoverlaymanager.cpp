// Manages the selection overlay widget and its glide animation
#include "selectionoverlaymanager.h"
#include "interactionstateholder.h"
#include "itemwidget.h"
#include "uiconstants.h"
#include <QColor>
#include <QPropertyAnimation>
#include <QWidget>
#include <cmath>

SelectionOverlayManager::SelectionOverlayManager(QObject *parent)
    : QObject(parent) {}

SelectionOverlayManager::~SelectionOverlayManager() {
  if (m_animation) {
    m_animation->stop();
  }
}

void SelectionOverlayManager::setParentWidget(QWidget *parent) {
  m_parentWidget = parent;
  // Recreate overlay with new parent if it exists
  if (m_overlay && m_parentWidget) {
    m_overlay->setParent(m_parentWidget);
  }
}

void SelectionOverlayManager::setGridContainer(QWidget *gridContainer) {
  m_gridContainer = gridContainer;
}

void SelectionOverlayManager::setInteractionState(
    InteractionStateHolder *state) {
  m_state = state;
}

void SelectionOverlayManager::setForceVisible(bool force) {
  if (m_forceVisible == force) {
    return;
  }
  m_forceVisible = force;
}

auto SelectionOverlayManager::shouldKeepVisible() const -> bool {
  return m_forceVisible;
}

void SelectionOverlayManager::ensureOverlay() {
  if (m_overlay.isNull() && m_parentWidget) {
    m_overlay = new QWidget(m_parentWidget);
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_overlay->setAttribute(Qt::WA_NoSystemBackground, true);
    updateOverlayStyle();
  }
}

void SelectionOverlayManager::updateOverlayStyle() {
  if (!m_overlay) {
    return;
  }

  QString borderColor;
  if (!ItemWidget::s_selectionColor.isEmpty() &&
      QColor::isValidColorName(ItemWidget::s_selectionColor)) {
    borderColor = ItemWidget::s_selectionColor;
  } else {
    borderColor = "palette(highlight)";
  }

  m_overlay->setStyleSheet(
      QString(
          "background: transparent; border:%1px solid %2; border-radius:%3px;")
          .arg(UIConstants::Widget::BORDER_WIDTH_SELECTION)
          .arg(borderColor)
          .arg(UIConstants::Widget::BORDER_RADIUS));
}

void SelectionOverlayManager::ensureAnimation() {
  if (m_animation.isNull() && m_overlay) {
    m_animation = new QPropertyAnimation(m_overlay, "geometry", this);
    m_animation->setEasingCurve(QEasingCurve::Linear);

    connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
      // If we're restarting the animation, don't run finish logic
      if (m_restartingAnim) {
        return;
      }

      // Clear GlideAnimating unless we should keep overlay visible
      if (!shouldKeepVisible()) {
        if (m_state) {
          m_state->setGlideAnimating(false);
        }
      }

      emit animationFinished();
    });
  }
}

void SelectionOverlayManager::showAtWidget(ItemWidget *widget) {
  if (!widget) {
    return;
  }
  ensureOverlay();
  if (!m_overlay) {
    return;
  }

  // Update style in case primary color changed (e.g., collection switch)
  updateOverlayStyle();

  QRect rect = overlayRectForWidget(widget);
  if (!rect.isValid()) {
    return;
  }

  m_overlay->setGeometry(rect);
  m_overlay->show();
  m_overlay->raise();
}

void SelectionOverlayManager::showAtRect(const QRect &rect) {
  if (!rect.isValid()) {
    return;
  }
  ensureOverlay();
  if (!m_overlay) {
    return;
  }

  // Update style in case primary color changed (e.g., collection switch)
  updateOverlayStyle();

  m_overlay->setGeometry(rect);
  m_overlay->show();
  m_overlay->raise();
}

void SelectionOverlayManager::hide() {
  if (m_overlay) {
    m_overlay->hide();
  }
}

void SelectionOverlayManager::raise() {
  if (m_overlay) {
    m_overlay->raise();
  }
}

auto SelectionOverlayManager::isVisible() const -> bool {
  return m_overlay && m_overlay->isVisible();
}

void SelectionOverlayManager::animateTo(const QRect &targetRect,
                                        const QRect &startRect) {
  ensureOverlay();
  ensureAnimation();

  if (!m_overlay || !m_animation) {
    return;
  }

  if (!targetRect.isValid()) {
    return;
  }

  QRect currentRect = startRect.isValid() ? startRect : m_overlay->geometry();

  // If overlay isn't visible or valid, start from target
  if (!m_overlay->isVisible() || !currentRect.isValid()) {
    currentRect = targetRect;
    m_overlay->setGeometry(currentRect);
  }

  // Show and raise overlay
  m_overlay->show();
  m_overlay->raise();

  // Set GlideAnimating property
  if (m_state) {
    m_state->setGlideAnimating(true);
  }

  // Stop running animation and get current position
  if (m_animation->state() == QAbstractAnimation::Running) {
    currentRect = m_overlay->geometry();
    m_restartingAnim = true;
    m_animation->stop();
    m_restartingAnim = false;
  }

  // Calculate animation duration based on distance
  int deltaX = std::abs(currentRect.center().x() - targetRect.center().x());
  int deltaY = std::abs(currentRect.center().y() - targetRect.center().y());
  double distance =
      std::sqrt(static_cast<double>(deltaX * deltaX + deltaY * deltaY));

  static constexpr double PIXELS_PER_SECOND =
      UIConstants::Selection::OVERLAY_GLIDE_PIXELS_PER_SECOND;
  static constexpr int MIN_DURATION = 50;
  static constexpr int MAX_DURATION =
      UIConstants::Selection::OVERLAY_GLIDE_MAX_DURATION_MS;

  int duration =
      static_cast<int>(std::round((distance / PIXELS_PER_SECOND) * 1000.0));
  duration = std::clamp(duration, MIN_DURATION, MAX_DURATION);

  // Start the glide animation
  m_animation->setDuration(duration);
  m_animation->setStartValue(currentRect);
  m_animation->setEndValue(targetRect);
  m_animation->start();
}

void SelectionOverlayManager::stopAnimation() {
  if (m_animation && m_animation->state() == QAbstractAnimation::Running) {
    m_animation->stop();
  }
}

auto SelectionOverlayManager::isAnimating() const -> bool {
  return m_animation && m_animation->state() == QAbstractAnimation::Running;
}

auto SelectionOverlayManager::geometry() const -> QRect {
  if (m_overlay) {
    return m_overlay->geometry();
  }
  return {};
}

void SelectionOverlayManager::setGeometry(const QRect &rect) {
  ensureOverlay();
  if (m_overlay && rect.isValid()) {
    m_overlay->setGeometry(rect);
  }
}

auto SelectionOverlayManager::overlayRectForWidget(ItemWidget *widget)
    -> QRect {
  if (!widget) {
    return {};
  }
  QRect borderRect = widget->selectionBorderRectInParent();
  if (borderRect.isValid()) {
    return borderRect;
  }

  QRect widgetRect = widget->geometry();
  if (!widgetRect.isValid()) {
    return {};
  }

  const int inset = UIConstants::CollectionIcon::ITEM_SPACING;
  return widgetRect.adjusted(-inset, -inset, inset, inset);
}

auto SelectionOverlayManager::overlayRectForPosition(const QPoint &pos,
                                                     int itemWidth,
                                                     int itemHeight) -> QRect {
  const int inset = UIConstants::CollectionIcon::ITEM_SPACING;
  return QRect(pos.x() - inset, pos.y() - inset, itemWidth + 2 * inset,
               itemHeight + 2 * inset);
}
