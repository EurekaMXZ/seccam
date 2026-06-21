#include "seccam/core/daemon_config.hpp"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <sstream>
#include <stdexcept>
#include <string>

#include "ini.h"

namespace fs = std::filesystem;

namespace seccam::core {
namespace {

struct IniContext {
  DaemonConfig *config = nullptr;
  std::string error;
};

std::uint32_t parse_u32_or_throw(const std::string &value, const char *field_name) {
  char *end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  if (value.empty() || end == nullptr || *end != '\0' || errno != 0) {
    throw std::runtime_error(std::string("invalid unsigned integer for ") + field_name + ": " +
                             value);
  }
  return static_cast<std::uint32_t>(parsed);
}

std::uint64_t parse_u64_or_throw(const std::string &value, const char *field_name) {
  char *end = nullptr;
  errno = 0;
  const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
  if (value.empty() || end == nullptr || *end != '\0' || errno != 0) {
    throw std::runtime_error(std::string("invalid unsigned integer for ") + field_name + ": " +
                             value);
  }
  return static_cast<std::uint64_t>(parsed);
}

int parse_i32_or_throw(const std::string &value, const char *field_name) {
  char *end = nullptr;
  errno = 0;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (value.empty() || end == nullptr || *end != '\0' || errno != 0) {
    throw std::runtime_error(std::string("invalid integer for ") + field_name + ": " + value);
  }
  return static_cast<int>(parsed);
}

float parse_float_or_throw(const std::string &value, const char *field_name) {
  char *end = nullptr;
  errno = 0;
  const float parsed = std::strtof(value.c_str(), &end);
  if (value.empty() || end == nullptr || *end != '\0' || errno != 0) {
    throw std::runtime_error(std::string("invalid float for ") + field_name + ": " + value);
  }
  return parsed;
}

void apply_runtime_value(RuntimeConfig &runtime, const std::string &name, const std::string &value) {
  if (name == "model_name") {
    runtime.model_name = value;
  } else if (name == "model_path") {
    runtime.model_path = value;
  } else if (name == "record_dir") {
    runtime.record_dir = value;
  } else if (name == "sensor_config_path") {
    runtime.sensor_config_path = value;
  } else if (name == "rtsp_stream_name") {
    runtime.rtsp_stream_name = value;
  } else if (name == "threshold") {
    runtime.threshold = parse_float_or_throw(value, name.c_str());
  } else if (name == "trigger_hits") {
    runtime.trigger_hits = parse_u32_or_throw(value, name.c_str());
  } else if (name == "clear_misses") {
    runtime.clear_misses = parse_u32_or_throw(value, name.c_str());
  } else if (name == "hold_seconds") {
    runtime.hold_seconds = parse_u32_or_throw(value, name.c_str());
  } else if (name == "min_record_seconds") {
    runtime.min_record_seconds = parse_u32_or_throw(value, name.c_str());
  } else if (name == "stream_width") {
    runtime.stream_width = parse_u32_or_throw(value, name.c_str());
  } else if (name == "stream_height") {
    runtime.stream_height = parse_u32_or_throw(value, name.c_str());
  } else if (name == "detect_width") {
    runtime.detect_width = parse_u32_or_throw(value, name.c_str());
  } else if (name == "detect_height") {
    runtime.detect_height = parse_u32_or_throw(value, name.c_str());
  } else if (name == "tdl_vb_width") {
    runtime.tdl_vb_width = parse_u32_or_throw(value, name.c_str());
  } else if (name == "tdl_vb_height") {
    runtime.tdl_vb_height = parse_u32_or_throw(value, name.c_str());
  } else if (name == "bitrate_kbps") {
    runtime.bitrate_kbps = parse_u32_or_throw(value, name.c_str());
  } else if (name == "rtsp_port") {
    runtime.rtsp_port = parse_u32_or_throw(value, name.c_str());
  } else if (name == "max_record_bytes") {
    runtime.max_record_bytes = parse_u64_or_throw(value, name.c_str());
  } else if (name == "max_segment_bytes") {
    runtime.max_segment_bytes = parse_u64_or_throw(value, name.c_str());
  } else if (name == "prebuffer_bytes") {
    runtime.prebuffer_bytes = parse_u64_or_throw(value, name.c_str());
  } else if (name == "person_class_id") {
    runtime.person_class_id = parse_i32_or_throw(value, name.c_str());
  } else if (name == "draw_text") {
    runtime.draw_text = parse_u32_or_throw(value, name.c_str()) != 0;
  }
}

int ini_handler(void *user, const char *section, const char *name, const char *value) {
  auto *context = static_cast<IniContext *>(user);
  try {
    const std::string section_name = section != nullptr ? section : "";
    const std::string key_name = name != nullptr ? name : "";
    const std::string raw_value = value != nullptr ? value : "";

    if (section_name == "ipc") {
      if (key_name == "socket_path") {
        context->config->socket_path = raw_value;
      } else if (key_name == "listen_backlog") {
        context->config->listen_backlog =
            static_cast<int>(parse_u32_or_throw(raw_value, key_name.c_str()));
      }
      return 1;
    }

    if (section_name == "runtime") {
      apply_runtime_value(context->config->runtime, key_name, raw_value);
      return 1;
    }
  } catch (const std::exception &error) {
    context->error = error.what();
    return 0;
  }

  return 1;
}

std::string scan_config_path(int argc, char **argv) {
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--config" && index + 1 < argc) {
      return argv[index + 1];
    }
    if (arg.rfind("--config=", 0) == 0) {
      return arg.substr(sizeof("--config=") - 1);
    }
  }
  return {};
}

void load_ini_file_or_throw(DaemonConfig &config) {
  if (!fs::exists(config.config_path)) {
    return;
  }

  IniContext context;
  context.config = &config;
  const int parse_result = ini_parse(config.config_path.c_str(), ini_handler, &context);
  if (parse_result < 0) {
    throw std::runtime_error("failed to open config file: " + config.config_path);
  }
  if (!context.error.empty()) {
    throw std::runtime_error("failed to parse config file " + config.config_path + ": " +
                             context.error);
  }
  if (parse_result > 0) {
    throw std::runtime_error("failed to parse config file " + config.config_path + " at line " +
                             std::to_string(parse_result));
  }
}

void validate_or_throw(const DaemonConfig &config) {
  const RuntimeConfig &runtime = config.runtime;
  if (config.socket_path.empty()) {
    throw std::runtime_error("socket_path must not be empty");
  }
  if (config.listen_backlog <= 0) {
    throw std::runtime_error("listen_backlog must be positive");
  }
  if (runtime.record_dir.empty()) {
    throw std::runtime_error("record_dir must not be empty");
  }
  if (runtime.threshold < 0.0f || runtime.threshold > 1.0f) {
    throw std::runtime_error("threshold must be within [0, 1]");
  }
  if (runtime.trigger_hits == 0 || runtime.clear_misses == 0 || runtime.stream_width == 0 ||
      runtime.stream_height == 0 || runtime.detect_width == 0 || runtime.detect_height == 0 ||
      runtime.tdl_vb_width == 0 || runtime.tdl_vb_height == 0 || runtime.bitrate_kbps == 0 ||
      runtime.rtsp_port == 0) {
    throw std::runtime_error("runtime size and detection counters must be positive");
  }
}

}  // namespace

void print_usage(const char *argv0) {
  std::fprintf(stdout,
               "Usage: %s [options]\n"
               "\n"
               "Daemon options:\n"
               "  --config PATH                 Config file, default /mnt/data/seccam/seccam-core.ini\n"
               "  --socket-path PATH            UDS socket path, default /var/run/seccam-core.sock\n"
               "  --listen-backlog N            UDS listen backlog, default 16\n"
               "\n"
               "Runtime options:\n"
               "  --model-name NAME\n"
               "  --model-path PATH\n"
               "  --record-dir PATH\n"
               "  --threshold FLOAT\n"
               "  --trigger-hits N\n"
               "  --clear-misses N\n"
               "  --hold-seconds N\n"
               "  --min-record-seconds N\n"
               "  --stream-width N\n"
               "  --stream-height N\n"
               "  --detect-width N\n"
               "  --detect-height N\n"
               "  --tdl-vb-width N\n"
               "  --tdl-vb-height N\n"
               "  --bitrate-kbps N\n"
               "  --rtsp-port N\n"
               "  --max-record-bytes N\n"
               "  --max-segment-bytes N\n"
               "  --prebuffer-bytes N\n"
               "  --sensor-config-path PATH\n"
               "  --rtsp-stream-name NAME\n"
               "  --person-class-id N\n"
               "  --draw-text 0|1\n"
               "  --help\n",
               argv0);
}

DaemonConfig load_daemon_config_or_throw(int argc, char **argv) {
  DaemonConfig config;

  const std::string explicit_config_path = scan_config_path(argc, argv);
  if (!explicit_config_path.empty()) {
    config.config_path = explicit_config_path;
  }
  load_ini_file_or_throw(config);

  static const option options[] = {
      {"config", required_argument, nullptr, 1001},
      {"socket-path", required_argument, nullptr, 1002},
      {"listen-backlog", required_argument, nullptr, 1003},
      {"model-name", required_argument, nullptr, 1004},
      {"model-path", required_argument, nullptr, 1005},
      {"record-dir", required_argument, nullptr, 1006},
      {"threshold", required_argument, nullptr, 1007},
      {"trigger-hits", required_argument, nullptr, 1008},
      {"clear-misses", required_argument, nullptr, 1009},
      {"hold-seconds", required_argument, nullptr, 1010},
      {"min-record-seconds", required_argument, nullptr, 1011},
      {"stream-width", required_argument, nullptr, 1012},
      {"stream-height", required_argument, nullptr, 1013},
      {"detect-width", required_argument, nullptr, 1014},
      {"detect-height", required_argument, nullptr, 1015},
      {"tdl-vb-width", required_argument, nullptr, 1016},
      {"tdl-vb-height", required_argument, nullptr, 1017},
      {"bitrate-kbps", required_argument, nullptr, 1018},
      {"rtsp-port", required_argument, nullptr, 1019},
      {"max-record-bytes", required_argument, nullptr, 1020},
      {"max-segment-bytes", required_argument, nullptr, 1021},
      {"prebuffer-bytes", required_argument, nullptr, 1022},
      {"sensor-config-path", required_argument, nullptr, 1023},
      {"rtsp-stream-name", required_argument, nullptr, 1024},
      {"person-class-id", required_argument, nullptr, 1025},
      {"draw-text", required_argument, nullptr, 1026},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  optind = 1;
  for (;;) {
    int option_index = 0;
    const int option = getopt_long(argc, argv, "h", options, &option_index);
    if (option == -1) {
      break;
    }

    switch (option) {
      case 1001:
        config.config_path = optarg;
        break;
      case 1002:
        config.socket_path = optarg;
        break;
      case 1003:
        config.listen_backlog = static_cast<int>(parse_u32_or_throw(optarg, "listen_backlog"));
        break;
      case 1004:
        config.runtime.model_name = optarg;
        break;
      case 1005:
        config.runtime.model_path = optarg;
        break;
      case 1006:
        config.runtime.record_dir = optarg;
        break;
      case 1007:
        config.runtime.threshold = parse_float_or_throw(optarg, "threshold");
        break;
      case 1008:
        config.runtime.trigger_hits = parse_u32_or_throw(optarg, "trigger_hits");
        break;
      case 1009:
        config.runtime.clear_misses = parse_u32_or_throw(optarg, "clear_misses");
        break;
      case 1010:
        config.runtime.hold_seconds = parse_u32_or_throw(optarg, "hold_seconds");
        break;
      case 1011:
        config.runtime.min_record_seconds = parse_u32_or_throw(optarg, "min_record_seconds");
        break;
      case 1012:
        config.runtime.stream_width = parse_u32_or_throw(optarg, "stream_width");
        break;
      case 1013:
        config.runtime.stream_height = parse_u32_or_throw(optarg, "stream_height");
        break;
      case 1014:
        config.runtime.detect_width = parse_u32_or_throw(optarg, "detect_width");
        break;
      case 1015:
        config.runtime.detect_height = parse_u32_or_throw(optarg, "detect_height");
        break;
      case 1016:
        config.runtime.tdl_vb_width = parse_u32_or_throw(optarg, "tdl_vb_width");
        break;
      case 1017:
        config.runtime.tdl_vb_height = parse_u32_or_throw(optarg, "tdl_vb_height");
        break;
      case 1018:
        config.runtime.bitrate_kbps = parse_u32_or_throw(optarg, "bitrate_kbps");
        break;
      case 1019:
        config.runtime.rtsp_port = parse_u32_or_throw(optarg, "rtsp_port");
        break;
      case 1020:
        config.runtime.max_record_bytes = parse_u64_or_throw(optarg, "max_record_bytes");
        break;
      case 1021:
        config.runtime.max_segment_bytes = parse_u64_or_throw(optarg, "max_segment_bytes");
        break;
      case 1022:
        config.runtime.prebuffer_bytes = parse_u64_or_throw(optarg, "prebuffer_bytes");
        break;
      case 1023:
        config.runtime.sensor_config_path = optarg;
        break;
      case 1024:
        config.runtime.rtsp_stream_name = optarg;
        break;
      case 1025:
        config.runtime.person_class_id = parse_i32_or_throw(optarg, "person_class_id");
        break;
      case 1026:
        config.runtime.draw_text = parse_u32_or_throw(optarg, "draw_text") != 0;
        break;
      case 'h':
        print_usage(argv[0]);
        std::exit(0);
      default:
        throw std::runtime_error("invalid command line option");
    }
  }

  validate_or_throw(config);
  return config;
}

void persist_daemon_config_or_throw(const DaemonConfig &config) {
  validate_or_throw(config);

  const fs::path config_path(config.config_path);
  if (!config_path.parent_path().empty()) {
    fs::create_directories(config_path.parent_path());
  }

  std::ofstream stream(config.config_path, std::ios::out | std::ios::trunc);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to write config file: " + config.config_path);
  }

  stream << "[ipc]\n";
  stream << "socket_path=" << config.socket_path << "\n";
  stream << "listen_backlog=" << config.listen_backlog << "\n\n";

  stream << "[runtime]\n";
  stream << "model_name=" << config.runtime.model_name << "\n";
  stream << "model_path=" << config.runtime.model_path << "\n";
  stream << "record_dir=" << config.runtime.record_dir << "\n";
  stream << "sensor_config_path=" << config.runtime.sensor_config_path << "\n";
  stream << "rtsp_stream_name=" << config.runtime.rtsp_stream_name << "\n";
  stream << "threshold=" << config.runtime.threshold << "\n";
  stream << "trigger_hits=" << config.runtime.trigger_hits << "\n";
  stream << "clear_misses=" << config.runtime.clear_misses << "\n";
  stream << "hold_seconds=" << config.runtime.hold_seconds << "\n";
  stream << "min_record_seconds=" << config.runtime.min_record_seconds << "\n";
  stream << "stream_width=" << config.runtime.stream_width << "\n";
  stream << "stream_height=" << config.runtime.stream_height << "\n";
  stream << "detect_width=" << config.runtime.detect_width << "\n";
  stream << "detect_height=" << config.runtime.detect_height << "\n";
  stream << "tdl_vb_width=" << config.runtime.tdl_vb_width << "\n";
  stream << "tdl_vb_height=" << config.runtime.tdl_vb_height << "\n";
  stream << "bitrate_kbps=" << config.runtime.bitrate_kbps << "\n";
  stream << "rtsp_port=" << config.runtime.rtsp_port << "\n";
  stream << "max_record_bytes=" << config.runtime.max_record_bytes << "\n";
  stream << "max_segment_bytes=" << config.runtime.max_segment_bytes << "\n";
  stream << "prebuffer_bytes=" << config.runtime.prebuffer_bytes << "\n";
  stream << "person_class_id=" << config.runtime.person_class_id << "\n";
  stream << "draw_text=" << (config.runtime.draw_text ? 1 : 0) << "\n";
}

}  // namespace seccam::core
