#include "seccam/core/runtime_state.hpp"

#include <algorithm>

namespace seccam::core {

RuntimeState::RuntimeState(RuntimeConfig initial_config)
    : config_(std::move(initial_config)), status_(default_core_status(config_)) {}

RuntimeConfig RuntimeState::config_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

CoreStatus RuntimeState::status_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

std::vector<EventMessage> RuntimeState::recent_events(std::size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (limit == 0 || events_.empty()) {
    return {};
  }

  const std::size_t first_index = events_.size() > limit ? events_.size() - limit : 0;
  return std::vector<EventMessage>(events_.begin() + static_cast<std::ptrdiff_t>(first_index),
                                   events_.end());
}

RuntimeConfig RuntimeState::apply_config(const RuntimeConfig &next_config) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = next_config;
  status_.model_name = config_.model_name;
  status_.stream_width = config_.stream_width;
  status_.stream_height = config_.stream_height;
  status_.detect_width = config_.detect_width;
  status_.detect_height = config_.detect_height;
  return config_;
}

void RuntimeState::set_recording_active(bool active, std::uint64_t started_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.recording = active;
  if (started_ms > 0) {
    status_.last_recording_start_ms = started_ms;
  }
}

void RuntimeState::set_detection_snapshot(std::vector<DetectionObject> objects,
                                          std::uint64_t detected_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.current_objects = std::move(objects);
  status_.last_detection_ms = detected_ms;
}

void RuntimeState::mark_components_ready(bool media_ready, bool inference_ready) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.media_ready = media_ready;
  status_.inference_ready = inference_ready;
}

void RuntimeState::set_pipeline_counters(std::uint32_t detection_fps, std::uint32_t encode_fps) {
  std::lock_guard<std::mutex> lock(mutex_);
  status_.detection_fps = detection_fps;
  status_.encode_fps = encode_fps;
}

void RuntimeState::record_detection_event(bool present, std::vector<DetectionObject> objects,
                                          std::uint64_t happened_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  EventMessage event;
  event.type = EventType::Detection;
  event.happened_ms = happened_ms;
  event.summary = present ? "target detected" : "target cleared";
  event.detection = DetectionEvent{present, std::move(objects)};
  append_event_locked(std::move(event));
}

void RuntimeState::record_recording_event(const std::string &file_path, bool active,
                                          std::uint64_t happened_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  EventMessage event;
  event.type = EventType::Recording;
  event.happened_ms = happened_ms;
  event.summary = active ? "recording started" : "recording stopped";
  event.recording = RecordingEvent{file_path, active};
  append_event_locked(std::move(event));
}

void RuntimeState::record_fault(const std::string &component, const std::string &message) {
  std::lock_guard<std::mutex> lock(mutex_);
  EventMessage event;
  event.type = EventType::Fault;
  event.happened_ms = unix_time_ms();
  event.summary = component + ": " + message;
  event.fault = FaultEvent{component, message};
  append_event_locked(std::move(event));
}

void RuntimeState::append_event_locked(EventMessage event) {
  events_.push_back(std::move(event));
  while (events_.size() > kMaxEvents) {
    events_.pop_front();
  }
}

}  // namespace seccam::core
