#include "seccam/core/types.hpp"

#include <chrono>

namespace seccam::core {

RuntimeConfig default_runtime_config() {
  RuntimeConfig config;
  config.record_dir = "/mnt/data/seccam/recordings";
  config.sensor_config_path = "/mnt/data/sensor_cfg.ini";
  config.rtsp_stream_name = "h264";
  return config;
}

CoreStatus default_core_status(const RuntimeConfig &config) {
  CoreStatus status;
  status.model_name = config.model_name;
  status.stream_width = config.stream_width;
  status.stream_height = config.stream_height;
  status.detect_width = config.detect_width;
  status.detect_height = config.detect_height;
  return status;
}

std::uint64_t unix_time_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string event_type_name(EventType type) {
  switch (type) {
    case EventType::Detection:
      return "EVENT_TYPE_DETECTION";
    case EventType::Recording:
      return "EVENT_TYPE_RECORDING";
    case EventType::Fault:
      return "EVENT_TYPE_FAULT";
  }
  return "EVENT_TYPE_UNSPECIFIED";
}

}  // namespace seccam::core
