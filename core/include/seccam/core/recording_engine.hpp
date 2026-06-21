#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "seccam/core/types.hpp"

namespace seccam::core {

class RuntimeState;

struct EncodedChunkView {
  const std::uint8_t *data = nullptr;
  std::size_t size = 0;
};

struct EncodedFrameView {
  std::vector<EncodedChunkView> chunks;
};

struct RecordingEngineConfig {
  std::string output_dir;
  std::uint64_t max_total_bytes = 0;
  std::uint64_t max_segment_bytes = 0;
  std::uint64_t max_prebuffer_bytes = 0;
  std::uint64_t hold_ms = 0;
  std::uint64_t min_record_ms = 0;
};

struct RecordingEngineSnapshot {
  bool recording = false;
  std::string current_path;
  std::uint64_t current_started_ms = 0;
  std::uint64_t current_file_bytes = 0;
  std::uint64_t buffered_bytes = 0;
  std::uint64_t last_trigger_ms = 0;
};

RecordingEngineConfig make_recording_engine_config(const RuntimeConfig &runtime_config);

class RecordingEngine {
 public:
  explicit RecordingEngine(RecordingEngineConfig config, RuntimeState *runtime_state = nullptr);
  ~RecordingEngine();

  RecordingEngine(const RecordingEngine &) = delete;
  RecordingEngine &operator=(const RecordingEngine &) = delete;

  void apply_config(RecordingEngineConfig config);
  RecordingEngineSnapshot snapshot() const;
  void consume_frame(const EncodedFrameView &frame, bool trigger_recording, std::uint64_t now_ms);
  void close();

 private:
  struct BufferedFrame {
    std::vector<std::uint8_t> data;
  };

  static std::uint64_t frame_size_bytes(const EncodedFrameView &frame);
  static BufferedFrame copy_frame(const EncodedFrameView &frame);

  void ensure_output_dir_locked() const;
  void clear_prebuffer_locked();
  void trim_prebuffer_locked();
  void append_prebuffer_locked(const EncodedFrameView &frame);
  void open_file_locked(std::uint64_t now_ms);
  void discard_current_file_locked();
  void close_file_locked();
  void flush_prebuffer_locked();
  void write_frame_locked(const EncodedFrameView &frame);
  void trim_directory_locked() const;
  void sync_runtime_state_locked() const;

  mutable std::mutex mutex_;
  RecordingEngineConfig config_;
  RuntimeState *runtime_state_ = nullptr;
  std::FILE *current_file_ = nullptr;
  std::string current_path_;
  std::uint64_t current_started_ms_ = 0;
  std::uint64_t current_file_bytes_ = 0;
  std::uint64_t buffered_bytes_ = 0;
  std::uint64_t last_trigger_ms_ = 0;
  std::deque<BufferedFrame> prebuffer_;
};

}  // namespace seccam::core
