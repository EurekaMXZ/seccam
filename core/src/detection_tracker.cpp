#include "seccam/core/detection_tracker.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

#include "seccam/core/runtime_state.hpp"

namespace seccam::core {
namespace {

DetectionTrackerConfig normalize_config(DetectionTrackerConfig config) {
  if (config.trigger_hits == 0) {
    throw std::invalid_argument("trigger_hits must be positive");
  }
  if (config.clear_misses == 0) {
    throw std::invalid_argument("clear_misses must be positive");
  }
  return config;
}

}  // namespace

DetectionTrackerConfig make_detection_tracker_config(const RuntimeConfig &runtime_config) {
  DetectionTrackerConfig config;
  config.trigger_hits = runtime_config.trigger_hits;
  config.clear_misses = runtime_config.clear_misses;
  return normalize_config(config);
}

DetectionTracker::DetectionTracker(DetectionTrackerConfig config, RuntimeState *runtime_state)
    : runtime_state_(runtime_state) {
  apply_config(std::move(config));
}

void DetectionTracker::apply_config(DetectionTrackerConfig config) {
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = normalize_config(std::move(config));
    if (!confirmed_present_ && hit_streak_ >= config_.trigger_hits) {
      confirmed_present_ = true;
    }
    if (confirmed_present_ && miss_streak_ >= config_.clear_misses) {
      confirmed_present_ = false;
    }
  } catch (const std::exception &error) {
    if (runtime_state_ != nullptr) {
      runtime_state_->record_fault("detection", error.what());
    }
    throw;
  }
}

void DetectionTracker::publish(std::vector<DetectionObject> objects, bool trigger_present,
                               std::uint64_t now_ms) {
  std::vector<DetectionObject> runtime_objects;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    objects_ = std::move(objects);
    last_update_ms_ = now_ms;

    if (trigger_present) {
      ++hit_streak_;
      miss_streak_ = 0;
      last_seen_ms_ = now_ms;
      if (!confirmed_present_ && hit_streak_ >= config_.trigger_hits) {
        confirmed_present_ = true;
      }
    } else {
      hit_streak_ = 0;
      ++miss_streak_;
      if (confirmed_present_ && miss_streak_ >= config_.clear_misses) {
        confirmed_present_ = false;
      }
    }

    runtime_objects = objects_;
  }

  if (runtime_state_ != nullptr) {
    runtime_state_->set_detection_snapshot(std::move(runtime_objects), now_ms);
  }
}

bool DetectionTracker::is_present(std::uint64_t now_ms, std::uint64_t stale_after_ms,
                                  std::uint64_t *last_seen_ms) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (last_seen_ms != nullptr) {
    *last_seen_ms = last_seen_ms_;
  }
  return present_locked(now_ms, stale_after_ms);
}

DetectionTrackerSnapshot DetectionTracker::snapshot(std::uint64_t now_ms,
                                                    std::uint64_t stale_after_ms) const {
  std::lock_guard<std::mutex> lock(mutex_);
  DetectionTrackerSnapshot snapshot;
  snapshot.objects = objects_;
  snapshot.present = present_locked(now_ms, stale_after_ms);
  snapshot.hit_streak = hit_streak_;
  snapshot.miss_streak = miss_streak_;
  snapshot.last_seen_ms = last_seen_ms_;
  snapshot.last_update_ms = last_update_ms_;
  return snapshot;
}

bool DetectionTracker::present_locked(std::uint64_t now_ms, std::uint64_t stale_after_ms) const {
  if (!confirmed_present_ || last_seen_ms_ == 0 || now_ms < last_seen_ms_) {
    return false;
  }
  return now_ms - last_seen_ms_ <= stale_after_ms;
}

}  // namespace seccam::core
