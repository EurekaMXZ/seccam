#include "seccam/core/recording_catalog.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace seccam::core {
namespace {

std::uint64_t file_time_to_unix_ms(fs::file_time_type file_time) {
  const auto system_now = std::chrono::system_clock::now();
  const auto file_now = fs::file_time_type::clock::now();
  const auto adjusted = file_time - file_now + system_now;
  return std::chrono::duration_cast<std::chrono::milliseconds>(adjusted.time_since_epoch()).count();
}

std::optional<std::uint64_t> parse_started_ms_from_name(const fs::path &path) {
  const std::string stem = path.stem().string();
  if (stem.size() < 19) {
    return std::nullopt;
  }

  const std::string timestamp_part = stem.substr(0, 15);
  const std::string millis_part = stem.substr(16);
  if (stem[15] != '-') {
    return std::nullopt;
  }

  std::tm time_parts = {};
  std::istringstream stream(timestamp_part);
  stream >> std::get_time(&time_parts, "%Y%m%d-%H%M%S");
  if (stream.fail()) {
    return std::nullopt;
  }

  char *end = nullptr;
  const unsigned long millis = std::strtoul(millis_part.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    return std::nullopt;
  }

  const std::time_t seconds = std::mktime(&time_parts);
  if (seconds < 0) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(seconds) * 1000ULL + millis;
}

}  // namespace

std::vector<RecordingFile> RecordingCatalog::list(const RuntimeConfig &config, std::uint32_t limit,
                                                  std::uint64_t newer_than_ms) const {
  std::vector<RecordingFile> recordings;
  if (config.record_dir.empty() || !fs::exists(config.record_dir)) {
    return recordings;
  }

  for (const auto &entry : fs::directory_iterator(config.record_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    RecordingFile file;
    file.path = entry.path().string();
    file.size = entry.file_size();
    file.ended_ms = file_time_to_unix_ms(entry.last_write_time());
    file.started_ms = parse_started_ms_from_name(entry.path()).value_or(file.ended_ms);
    if (file.ended_ms < newer_than_ms) {
      continue;
    }
    recordings.push_back(std::move(file));
  }

  std::sort(recordings.begin(), recordings.end(),
            [](const RecordingFile &left, const RecordingFile &right) {
              if (left.started_ms != right.started_ms) {
                return left.started_ms > right.started_ms;
              }
              return left.path < right.path;
            });

  if (limit > 0 && recordings.size() > limit) {
    recordings.resize(limit);
  }
  return recordings;
}

}  // namespace seccam::core
