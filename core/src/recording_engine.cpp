#include "seccam/core/recording_engine.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "seccam/core/runtime_state.hpp"

namespace fs = std::filesystem;

namespace seccam::core {
namespace {

struct FileEntry {
  fs::path path;
  fs::file_time_type modified_at;
  std::uint64_t size = 0;
};

bool file_entry_less(const FileEntry &left, const FileEntry &right) {
  if (left.modified_at != right.modified_at) {
    return left.modified_at < right.modified_at;
  }
  return left.path.string() < right.path.string();
}

std::string format_recording_timestamp(std::time_t seconds) {
  std::tm time_parts = {};
  localtime_r(&seconds, &time_parts);

  char buffer[32] = {};
  if (std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &time_parts) == 0) {
    throw std::runtime_error("failed to format recording timestamp");
  }
  return buffer;
}

std::string make_recording_path(const std::string &output_dir, std::uint64_t now_ms) {
  const auto real_time_ms = unix_time_ms();
  const std::time_t now_seconds = static_cast<std::time_t>(real_time_ms / 1000ULL);

  std::ostringstream stream;
  stream << format_recording_timestamp(now_seconds) << '-' << (now_ms % 1000ULL) << ".h264";
  return (fs::path(output_dir) / stream.str()).string();
}

RecordingEngineConfig normalize_config(RecordingEngineConfig config) {
  if (config.max_total_bytes > 0 &&
      (config.max_segment_bytes == 0 || config.max_segment_bytes > config.max_total_bytes)) {
    config.max_segment_bytes = config.max_total_bytes;
  }
  return config;
}

}  // namespace

RecordingEngineConfig make_recording_engine_config(const RuntimeConfig &runtime_config) {
  RecordingEngineConfig config;
  config.output_dir = runtime_config.record_dir;
  config.max_total_bytes = runtime_config.max_record_bytes;
  config.max_segment_bytes = runtime_config.max_segment_bytes;
  config.max_prebuffer_bytes = runtime_config.prebuffer_bytes;
  config.hold_ms = static_cast<std::uint64_t>(runtime_config.hold_seconds) * 1000ULL;
  config.min_record_ms = static_cast<std::uint64_t>(runtime_config.min_record_seconds) * 1000ULL;
  return normalize_config(std::move(config));
}

RecordingEngine::RecordingEngine(RecordingEngineConfig config, RuntimeState *runtime_state)
    : runtime_state_(runtime_state) {
  apply_config(std::move(config));
}

RecordingEngine::~RecordingEngine() {
  try {
    close();
  } catch (...) {
  }
}

void RecordingEngine::apply_config(RecordingEngineConfig config) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    config_ = normalize_config(std::move(config));
    ensure_output_dir_locked();
    if (config_.max_prebuffer_bytes == 0) {
      clear_prebuffer_locked();
    } else {
      trim_prebuffer_locked();
    }
  } catch (const std::exception &error) {
    if (runtime_state_ != nullptr) {
      runtime_state_->record_fault("recording", error.what());
    }
    throw;
  }
}

RecordingEngineSnapshot RecordingEngine::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  RecordingEngineSnapshot snapshot;
  snapshot.recording = current_file_ != nullptr;
  snapshot.current_path = current_path_;
  snapshot.current_started_ms = current_started_ms_;
  snapshot.current_file_bytes = current_file_bytes_;
  snapshot.buffered_bytes = buffered_bytes_;
  snapshot.last_trigger_ms = last_trigger_ms_;
  return snapshot;
}

void RecordingEngine::consume_frame(const EncodedFrameView &frame, bool trigger_recording,
                                    std::uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    bool current_frame_written_from_prebuffer = false;

    if (current_file_ == nullptr) {
      append_prebuffer_locked(frame);
    }

    if (trigger_recording) {
      last_trigger_ms_ = now_ms;
      if (current_file_ == nullptr) {
        if (config_.max_total_bytes > 0) {
          trim_directory_locked();
        }
        open_file_locked(now_ms);
        try {
          flush_prebuffer_locked();
        } catch (...) {
          discard_current_file_locked();
          throw;
        }
        current_frame_written_from_prebuffer = config_.max_prebuffer_bytes > 0;
      }
    }

    if (current_file_ != nullptr && !current_frame_written_from_prebuffer) {
      try {
        write_frame_locked(frame);
      } catch (...) {
        discard_current_file_locked();
        throw;
      }
    }

    if (current_file_ != nullptr && config_.max_segment_bytes > 0 &&
        current_file_bytes_ >= config_.max_segment_bytes) {
      const bool keep_recording =
          trigger_recording || (now_ms - last_trigger_ms_ < config_.hold_ms);
      close_file_locked();
      if (keep_recording) {
        open_file_locked(now_ms);
      }
    }

    if (current_file_ != nullptr && !trigger_recording) {
      const std::uint64_t held_for_ms = now_ms - last_trigger_ms_;
      const std::uint64_t record_age_ms = now_ms - current_started_ms_;
      if (held_for_ms >= config_.hold_ms && record_age_ms >= config_.min_record_ms) {
        close_file_locked();
      }
    }
  } catch (const std::exception &error) {
    if (runtime_state_ != nullptr) {
      runtime_state_->record_fault("recording", error.what());
    }
    throw;
  }
}

void RecordingEngine::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    close_file_locked();
    clear_prebuffer_locked();
  } catch (const std::exception &error) {
    if (runtime_state_ != nullptr) {
      runtime_state_->record_fault("recording", error.what());
    }
    throw;
  }
}

std::uint64_t RecordingEngine::frame_size_bytes(const EncodedFrameView &frame) {
  std::uint64_t total = 0;
  for (const EncodedChunkView &chunk : frame.chunks) {
    total += static_cast<std::uint64_t>(chunk.size);
  }
  return total;
}

RecordingEngine::BufferedFrame RecordingEngine::copy_frame(const EncodedFrameView &frame) {
  BufferedFrame buffered_frame;
  buffered_frame.data.reserve(static_cast<std::size_t>(frame_size_bytes(frame)));

  for (const EncodedChunkView &chunk : frame.chunks) {
    if (chunk.size == 0) {
      continue;
    }
    if (chunk.data == nullptr) {
      throw std::runtime_error("recording frame contains a null chunk");
    }
    buffered_frame.data.insert(buffered_frame.data.end(), chunk.data, chunk.data + chunk.size);
  }

  return buffered_frame;
}

void RecordingEngine::ensure_output_dir_locked() const {
  if (config_.output_dir.empty()) {
    throw std::runtime_error("recording output directory must not be empty");
  }

  std::error_code error;
  fs::create_directories(config_.output_dir, error);
  if (error && !fs::exists(config_.output_dir)) {
    throw std::runtime_error("failed to create recording directory " + config_.output_dir + ": " +
                             error.message());
  }
}

void RecordingEngine::clear_prebuffer_locked() {
  prebuffer_.clear();
  buffered_bytes_ = 0;
}

void RecordingEngine::trim_prebuffer_locked() {
  while (config_.max_prebuffer_bytes > 0 && buffered_bytes_ > config_.max_prebuffer_bytes &&
         prebuffer_.size() > 1) {
    buffered_bytes_ -= static_cast<std::uint64_t>(prebuffer_.front().data.size());
    prebuffer_.pop_front();
  }
}

void RecordingEngine::append_prebuffer_locked(const EncodedFrameView &frame) {
  if (config_.max_prebuffer_bytes == 0) {
    return;
  }

  BufferedFrame buffered_frame = copy_frame(frame);
  buffered_bytes_ += static_cast<std::uint64_t>(buffered_frame.data.size());
  prebuffer_.push_back(std::move(buffered_frame));
  trim_prebuffer_locked();
}

void RecordingEngine::open_file_locked(std::uint64_t now_ms) {
  ensure_output_dir_locked();

  current_path_ = make_recording_path(config_.output_dir, now_ms);
  current_file_ = std::fopen(current_path_.c_str(), "wb");
  if (current_file_ == nullptr) {
    throw std::runtime_error("failed to open recording file " + current_path_ + ": " +
                             std::strerror(errno));
  }

  current_started_ms_ = now_ms;
  current_file_bytes_ = 0;
  last_trigger_ms_ = now_ms;
  sync_runtime_state_locked();
}

void RecordingEngine::discard_current_file_locked() {
  if (current_file_ == nullptr) {
    return;
  }

  const std::uint64_t started_ms = current_started_ms_;
  std::fclose(current_file_);
  current_file_ = nullptr;
  current_path_.clear();
  current_started_ms_ = 0;
  current_file_bytes_ = 0;
  if (runtime_state_ != nullptr) {
    runtime_state_->set_recording_active(false, started_ms);
  }
}

void RecordingEngine::close_file_locked() {
  if (current_file_ == nullptr) {
    return;
  }

  discard_current_file_locked();
  if (config_.max_total_bytes > 0) {
    trim_directory_locked();
  }
}

void RecordingEngine::flush_prebuffer_locked() {
  for (const BufferedFrame &frame : prebuffer_) {
    if (!frame.data.empty() &&
        std::fwrite(frame.data.data(), 1, frame.data.size(), current_file_) != frame.data.size()) {
      clear_prebuffer_locked();
      throw std::runtime_error("failed to flush prebuffer frame");
    }
    current_file_bytes_ += static_cast<std::uint64_t>(frame.data.size());
  }

  clear_prebuffer_locked();
}

void RecordingEngine::write_frame_locked(const EncodedFrameView &frame) {
  for (const EncodedChunkView &chunk : frame.chunks) {
    if (chunk.size == 0) {
      continue;
    }
    if (chunk.data == nullptr) {
      throw std::runtime_error("recording frame contains a null chunk");
    }
    if (std::fwrite(chunk.data, 1, chunk.size, current_file_) != chunk.size) {
      throw std::runtime_error("failed to write recording data");
    }
    current_file_bytes_ += static_cast<std::uint64_t>(chunk.size);
  }
}

void RecordingEngine::trim_directory_locked() const {
  if (config_.max_total_bytes == 0 || config_.output_dir.empty()) {
    return;
  }

  try {
    std::vector<FileEntry> entries;
    std::uint64_t total_size = 0;

    for (const auto &entry : fs::directory_iterator(config_.output_dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }

      FileEntry file_entry;
      file_entry.path = entry.path();
      file_entry.modified_at = entry.last_write_time();
      file_entry.size = entry.file_size();
      total_size += file_entry.size;
      entries.push_back(std::move(file_entry));
    }

    std::sort(entries.begin(), entries.end(), file_entry_less);
    for (const FileEntry &entry : entries) {
      if (total_size <= config_.max_total_bytes) {
        break;
      }
      if (fs::remove(entry.path)) {
        total_size -= entry.size;
      }
    }
  } catch (const std::exception &error) {
    if (runtime_state_ != nullptr) {
      runtime_state_->record_fault("recording", error.what());
    }
  }
}

void RecordingEngine::sync_runtime_state_locked() const {
  if (runtime_state_ != nullptr) {
    runtime_state_->set_recording_active(current_file_ != nullptr, current_started_ms_);
  }
}

}  // namespace seccam::core
