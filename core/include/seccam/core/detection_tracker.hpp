#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "seccam/core/types.hpp"

namespace seccam::core {

class RuntimeState;

struct DetectionTrackerConfig {
  std::uint32_t trigger_hits = 3;
  std::uint32_t clear_misses = 2;
};

struct DetectionTrackerSnapshot {
  std::vector<DetectionObject> objects;
  bool present = false;
  std::uint32_t hit_streak = 0;
  std::uint32_t miss_streak = 0;
  std::uint64_t last_seen_ms = 0;
  std::uint64_t last_update_ms = 0;
};

DetectionTrackerConfig make_detection_tracker_config(const RuntimeConfig &runtime_config);

class DetectionTracker {
 public:
  explicit DetectionTracker(DetectionTrackerConfig config, RuntimeState *runtime_state = nullptr);

  DetectionTracker(const DetectionTracker &) = delete;
  DetectionTracker &operator=(const DetectionTracker &) = delete;

  void apply_config(DetectionTrackerConfig config);
  void publish(std::vector<DetectionObject> objects, bool trigger_present, std::uint64_t now_ms);
  bool is_present(std::uint64_t now_ms, std::uint64_t stale_after_ms,
                  std::uint64_t *last_seen_ms = nullptr) const;
  DetectionTrackerSnapshot snapshot(std::uint64_t now_ms, std::uint64_t stale_after_ms) const;

 private:
  bool present_locked(std::uint64_t now_ms, std::uint64_t stale_after_ms) const;

  mutable std::mutex mutex_;
  DetectionTrackerConfig config_;
  RuntimeState *runtime_state_ = nullptr;
  std::vector<DetectionObject> objects_;
  std::uint32_t hit_streak_ = 0;
  std::uint32_t miss_streak_ = 0;
  bool confirmed_present_ = false;
  std::uint64_t last_seen_ms_ = 0;
  std::uint64_t last_update_ms_ = 0;
};

}  // namespace seccam::core
