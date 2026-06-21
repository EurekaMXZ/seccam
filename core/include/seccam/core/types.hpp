#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seccam::core {

struct DetectionObject {
  std::string label;
  float score = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;
  float x2 = 0.0f;
  float y2 = 0.0f;
};

struct RecordingFile {
  std::string path;
  std::uint64_t size = 0;
  std::uint64_t started_ms = 0;
  std::uint64_t ended_ms = 0;
};

struct RuntimeConfig {
  std::string model_name;
  std::string model_path;
  std::string record_dir;
  std::string sensor_config_path;
  std::string rtsp_stream_name;
  float threshold = 0.5f;
  std::uint32_t trigger_hits = 3;
  std::uint32_t clear_misses = 2;
  std::uint32_t hold_seconds = 10;
  std::uint32_t min_record_seconds = 5;
  std::uint32_t stream_width = 1280;
  std::uint32_t stream_height = 720;
  std::uint32_t detect_width = 640;
  std::uint32_t detect_height = 384;
  std::uint32_t tdl_vb_width = 640;
  std::uint32_t tdl_vb_height = 384;
  std::uint32_t bitrate_kbps = 2048;
  std::uint32_t rtsp_port = 554;
  std::uint64_t max_record_bytes = 1024ULL * 1024ULL * 1024ULL;
  std::uint64_t max_segment_bytes = 64ULL * 1024ULL * 1024ULL;
  std::uint64_t prebuffer_bytes = 4ULL * 1024ULL * 1024ULL;
  int person_class_id = -1;
  bool draw_text = true;
};

struct CoreStatus {
  bool media_ready = false;
  bool inference_ready = false;
  bool recording = false;
  std::string model_name;
  std::uint32_t stream_width = 0;
  std::uint32_t stream_height = 0;
  std::uint32_t detect_width = 0;
  std::uint32_t detect_height = 0;
  std::uint32_t detection_fps = 0;
  std::uint32_t encode_fps = 0;
  std::uint64_t last_detection_ms = 0;
  std::uint64_t last_recording_start_ms = 0;
  std::vector<DetectionObject> current_objects;
};

enum class EventType {
  Detection,
  Recording,
  Fault,
};

struct DetectionEvent {
  bool present = false;
  std::vector<DetectionObject> objects;
};

struct RecordingEvent {
  std::string file_path;
  bool active = false;
};

struct FaultEvent {
  std::string component;
  std::string message;
};

struct EventMessage {
  EventType type = EventType::Fault;
  std::uint64_t happened_ms = 0;
  std::string summary;
  std::optional<DetectionEvent> detection;
  std::optional<RecordingEvent> recording;
  std::optional<FaultEvent> fault;
};

RuntimeConfig default_runtime_config();
CoreStatus default_core_status(const RuntimeConfig &config);
std::uint64_t unix_time_ms();
std::string event_type_name(EventType type);

}  // namespace seccam::core
