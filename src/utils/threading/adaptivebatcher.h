#ifndef ADAPTIVEBATCHER_H
#define ADAPTIVEBATCHER_H

#include <deque>
#include <QMutex>
#include <QMutexLocker>

/**
 * @brief Adaptive batch sizing based on measured performance.
 *
 * Tracks batch processing times and dynamically adjusts batch size
 * to maintain a target processing time. Uses exponential moving average
 * to smooth out timing variations.
 *
 * Thread-safe: all methods are mutex-guarded. In practice it is driven from the
 * GUI thread (the viewport dispatcher) and worker completion handlers; the
 * locking lets those callers share it without each re-synchronising
 * (Kartend-ll86n).
 */
class AdaptiveBatcher {
public:
  struct Config {
    int initialBatchSize;   ///< Starting batch size
    int minBatchSize;       ///< Minimum batch size
    int maxBatchSize;       ///< Maximum batch size
    int targetTimeMs;       ///< Target processing time per batch
    double smoothingFactor; ///< EMA smoothing (0-1, higher = more responsive)
    int historySize;        ///< Number of samples to keep for analysis

    // Default configuration
    static Config defaults() { return Config{10, 2, 50, 50, 0.3, 10}; }
  };

  explicit AdaptiveBatcher(const Config &config)
      : m_config(config), m_currentBatchSize(config.initialBatchSize), m_avgTimePerItem(0.0) {}

  // Default constructor uses default config
  AdaptiveBatcher() : AdaptiveBatcher(Config::defaults()) {}

  /// Record performance for a completed batch.
  ///
  /// Passing elapsed time explicitly avoids shared timer state, which can be
  /// corrupted when multiple batches overlap (common with QtConcurrent).
  /// Returns the recommended batch size for the next operation.
  int observeBatch(int itemsProcessed, qint64 elapsedMs) {
    // Lock first: the itemsProcessed<=0 early return reads m_currentBatchSize,
    // which every other path here mutates under m_mutex. Reading a non-atomic
    // int without the lock is a data race (TSan) on a class that advertises
    // thread safety (Kartend-u7tw).
    QMutexLocker lock(&m_mutex);
    if (itemsProcessed <= 0) {
      return m_currentBatchSize;
    }
    double timePerItem = static_cast<double>(elapsedMs) / itemsProcessed;

    // Update exponential moving average
    if (m_avgTimePerItem <= 0.0) {
      m_avgTimePerItem = timePerItem;
    } else {
      m_avgTimePerItem = m_config.smoothingFactor * timePerItem +
                         (1.0 - m_config.smoothingFactor) * m_avgTimePerItem;
    }

    // Keep history for analysis
    m_history.push_back({itemsProcessed, elapsedMs, timePerItem});
    while (m_history.size() > static_cast<size_t>(m_config.historySize)) {
      m_history.pop_front();
    }

    // Calculate optimal batch size to hit target time
    if (m_avgTimePerItem > 0.0) {
      double optimalSize = m_config.targetTimeMs / m_avgTimePerItem;
      int newSize = static_cast<int>(optimalSize);

      // Apply gradual adjustment (max 50% change per batch)
      int maxChange = qMax(1, m_currentBatchSize / 2);
      if (newSize > m_currentBatchSize) {
        newSize = qMin(newSize, m_currentBatchSize + maxChange);
      } else {
        newSize = qMax(newSize, m_currentBatchSize - maxChange);
      }

      // Clamp to configured bounds
      m_currentBatchSize = qBound(m_config.minBatchSize, newSize, m_config.maxBatchSize);
    }

    return m_currentBatchSize;
  }

  /// Get the current recommended batch size
  [[nodiscard]] int currentBatchSize() const {
    QMutexLocker lock(&m_mutex);
    return m_currentBatchSize;
  }

  /// Get performance statistics as JSON-like struct
  struct Stats {
    int currentBatchSize;
    double avgTimePerItemMs;
    int samplesCollected;
    int targetTimeMs;
  };

  [[nodiscard]] Stats stats() const {
    QMutexLocker lock(&m_mutex);
    return {m_currentBatchSize, m_avgTimePerItem, static_cast<int>(m_history.size()),
            m_config.targetTimeMs};
  }

private:
  struct Sample {
    int itemCount;
    qint64 elapsedMs;
    double timePerItem;
  };

  Config m_config;
  int m_currentBatchSize;
  double m_avgTimePerItem;
  std::deque<Sample> m_history;
  mutable QMutex m_mutex;
};

#endif // ADAPTIVEBATCHER_H
