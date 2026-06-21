#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "seccam/core/types.hpp"

namespace seccam::core {

class RuntimeState {
 public:
  explicit RuntimeState(RuntimeConfig initial_config);

  RuntimeConfig config_snapshot() const;
  CoreStatus status_snapshot() const;
  std::vector<EventMessage> recent_events(std::size_t limit) const;

  RuntimeConfig apply_config(const RuntimeConfig &next_config);
  void set_recording_active(bool active, std::uint64_t started_ms);
  void set_detection_snapshot(std::vector<DetectionObject> objects, std::uint64_t detected_ms);
  void mark_components_ready(bool media_ready, bool inference_ready);
  void set_pipeline_counters(std::uint32_t detection_fps, std::uint32_t encode_fps);
  void record_detection_event(bool present, std::vector<DetectionObject> objects,
                              std::uint64_t happened_ms);
  void record_recording_event(const std::string &file_path, bool active, std::uint64_t happened_ms);
  void record_fault(const std::string &component, const std::string &message);

 private:
  void append_event_locked(EventMessage event);

  mutable std::mutex mutex_;
  RuntimeConfig config_;
  CoreStatus status_;
  std::deque<EventMessage> events_;
  static constexpr std::size_t kMaxEvents = 128;
};

}  // namespace seccam::core
