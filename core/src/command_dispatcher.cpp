#include "seccam/core/command_dispatcher.hpp"

#include <utility>

#include <nlohmann/json.hpp>

namespace seccam::core {
namespace {

using json = nlohmann::json;

json detection_object_to_json(const DetectionObject &object) {
  return json{
      {"label", object.label}, {"score", object.score}, {"x1", object.x1},
      {"y1", object.y1},       {"x2", object.x2},       {"y2", object.y2},
  };
}

json runtime_config_to_json(const RuntimeConfig &config) {
  return json{
      {"model_name", config.model_name},
      {"model_path", config.model_path},
      {"record_dir", config.record_dir},
      {"sensor_config_path", config.sensor_config_path},
      {"rtsp_stream_name", config.rtsp_stream_name},
      {"threshold", config.threshold},
      {"trigger_hits", config.trigger_hits},
      {"clear_misses", config.clear_misses},
      {"hold_seconds", config.hold_seconds},
      {"min_record_seconds", config.min_record_seconds},
      {"stream_width", config.stream_width},
      {"stream_height", config.stream_height},
      {"detect_width", config.detect_width},
      {"detect_height", config.detect_height},
      {"tdl_vb_width", config.tdl_vb_width},
      {"tdl_vb_height", config.tdl_vb_height},
      {"bitrate_kbps", config.bitrate_kbps},
      {"rtsp_port", config.rtsp_port},
      {"max_record_bytes", config.max_record_bytes},
      {"max_segment_bytes", config.max_segment_bytes},
      {"prebuffer_bytes", config.prebuffer_bytes},
      {"person_class_id", config.person_class_id},
      {"draw_text", config.draw_text},
  };
}

void validate_runtime_config_or_throw(const DaemonConfig &config) {
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

RuntimeConfig merge_runtime_config(const RuntimeConfig &base, const json &payload) {
  RuntimeConfig merged = base;
  if (payload.contains("model_name")) {
    merged.model_name = payload.at("model_name").get<std::string>();
  }
  if (payload.contains("model_path")) {
    merged.model_path = payload.at("model_path").get<std::string>();
  }
  if (payload.contains("record_dir")) {
    merged.record_dir = payload.at("record_dir").get<std::string>();
  }
  if (payload.contains("sensor_config_path")) {
    merged.sensor_config_path = payload.at("sensor_config_path").get<std::string>();
  }
  if (payload.contains("rtsp_stream_name")) {
    merged.rtsp_stream_name = payload.at("rtsp_stream_name").get<std::string>();
  }
  if (payload.contains("threshold")) {
    merged.threshold = payload.at("threshold").get<float>();
  }
  if (payload.contains("trigger_hits")) {
    merged.trigger_hits = payload.at("trigger_hits").get<std::uint32_t>();
  }
  if (payload.contains("clear_misses")) {
    merged.clear_misses = payload.at("clear_misses").get<std::uint32_t>();
  }
  if (payload.contains("hold_seconds")) {
    merged.hold_seconds = payload.at("hold_seconds").get<std::uint32_t>();
  }
  if (payload.contains("min_record_seconds")) {
    merged.min_record_seconds = payload.at("min_record_seconds").get<std::uint32_t>();
  }
  if (payload.contains("stream_width")) {
    merged.stream_width = payload.at("stream_width").get<std::uint32_t>();
  }
  if (payload.contains("stream_height")) {
    merged.stream_height = payload.at("stream_height").get<std::uint32_t>();
  }
  if (payload.contains("detect_width")) {
    merged.detect_width = payload.at("detect_width").get<std::uint32_t>();
  }
  if (payload.contains("detect_height")) {
    merged.detect_height = payload.at("detect_height").get<std::uint32_t>();
  }
  if (payload.contains("tdl_vb_width")) {
    merged.tdl_vb_width = payload.at("tdl_vb_width").get<std::uint32_t>();
  }
  if (payload.contains("tdl_vb_height")) {
    merged.tdl_vb_height = payload.at("tdl_vb_height").get<std::uint32_t>();
  }
  if (payload.contains("bitrate_kbps")) {
    merged.bitrate_kbps = payload.at("bitrate_kbps").get<std::uint32_t>();
  }
  if (payload.contains("rtsp_port")) {
    merged.rtsp_port = payload.at("rtsp_port").get<std::uint32_t>();
  }
  if (payload.contains("max_record_bytes")) {
    merged.max_record_bytes = payload.at("max_record_bytes").get<std::uint64_t>();
  }
  if (payload.contains("max_segment_bytes")) {
    merged.max_segment_bytes = payload.at("max_segment_bytes").get<std::uint64_t>();
  }
  if (payload.contains("prebuffer_bytes")) {
    merged.prebuffer_bytes = payload.at("prebuffer_bytes").get<std::uint64_t>();
  }
  if (payload.contains("person_class_id")) {
    merged.person_class_id = payload.at("person_class_id").get<int>();
  }
  if (payload.contains("draw_text")) {
    merged.draw_text = payload.at("draw_text").get<bool>();
  }
  return merged;
}

json core_status_to_json(const CoreStatus &status) {
  json objects = json::array();
  for (const auto &object : status.current_objects) {
    objects.push_back(detection_object_to_json(object));
  }

  return json{
      {"media_ready", status.media_ready},
      {"inference_ready", status.inference_ready},
      {"recording", status.recording},
      {"model_name", status.model_name},
      {"stream_width", status.stream_width},
      {"stream_height", status.stream_height},
      {"detect_width", status.detect_width},
      {"detect_height", status.detect_height},
      {"detection_fps", status.detection_fps},
      {"encode_fps", status.encode_fps},
      {"last_detection_ms", status.last_detection_ms},
      {"last_recording_start_ms", status.last_recording_start_ms},
      {"current_objects", std::move(objects)},
  };
}

json recording_file_to_json(const RecordingFile &recording_file) {
  return json{
      {"path", recording_file.path},
      {"size", recording_file.size},
      {"started_ms", recording_file.started_ms},
      {"ended_ms", recording_file.ended_ms},
  };
}

std::string event_type_to_json_name(EventType type) {
  switch (type) {
    case EventType::Detection:
      return "detection";
    case EventType::Recording:
      return "recording";
    case EventType::Fault:
      return "fault";
  }
  return "fault";
}

json event_message_to_json(const EventMessage &event) {
  json payload = {
      {"event_type", event_type_to_json_name(event.type)},
      {"happened_ms", event.happened_ms},
      {"summary", event.summary},
      {"detection", nullptr},
      {"recording", nullptr},
      {"fault", nullptr},
  };

  if (event.detection.has_value()) {
    json objects = json::array();
    for (const auto &object : event.detection->objects) {
      objects.push_back(detection_object_to_json(object));
    }
    payload["detection"] = {
        {"present", event.detection->present},
        {"objects", std::move(objects)},
    };
  }

  if (event.recording.has_value()) {
    payload["recording"] = {
        {"file_path", event.recording->file_path},
        {"active", event.recording->active},
    };
  }

  if (event.fault.has_value()) {
    payload["fault"] = {
        {"component", event.fault->component},
        {"message", event.fault->message},
    };
  }

  return payload;
}

json error_reply(std::uint64_t request_id, std::uint32_t code, const std::string &message) {
  return json{
      {"request_id", request_id},
      {"error_reply", {{"code", code}, {"message", message}}},
  };
}

}  // namespace

CommandDispatcher::CommandDispatcher(RuntimeState &runtime_state, DaemonConfig &daemon_config,
                                     ConfigApplyHandler config_apply_handler)
    : runtime_state_(runtime_state),
      daemon_config_(daemon_config),
      config_apply_handler_(std::move(config_apply_handler)) {}

std::string CommandDispatcher::dispatch(const std::string &payload) {
  std::uint64_t request_id = 0;
  try {
    const json request = json::parse(payload);
    request_id = request.value("request_id", 0ULL);

    if (request.contains("status_request")) {
      json response;
      response["request_id"] = request_id;
      response["status_reply"] = {{"status", core_status_to_json(runtime_state_.status_snapshot())}};
      return response.dump();
    }

    if (request.contains("config_request")) {
      const json &config_request = request.at("config_request");
      if (config_request.contains("get")) {
        json response;
        response["request_id"] = request_id;
        response["config_reply"] = {
            {"current", runtime_config_to_json(runtime_state_.config_snapshot())},
            {"applied", true},
        };
        return response.dump();
      }

      if (config_request.contains("set")) {
        const json &set_request = config_request.at("set");
        const json &config_payload = set_request.at("config");

        std::lock_guard<std::mutex> lock(config_mutex_);
        DaemonConfig next = daemon_config_;
        try {
          next.runtime = merge_runtime_config(runtime_state_.config_snapshot(), config_payload);
          validate_runtime_config_or_throw(next);
        } catch (const std::invalid_argument &error) {
          return error_reply(request_id, 400, error.what()).dump();
        } catch (const nlohmann::json::exception &error) {
          return error_reply(request_id, 400, error.what()).dump();
        } catch (const std::runtime_error &error) {
          return error_reply(request_id, 400, error.what()).dump();
        }

        if (config_apply_handler_) {
          config_apply_handler_(next.runtime);
        }
        persist_daemon_config_or_throw(next);
        daemon_config_ = next;
        runtime_state_.apply_config(next.runtime);

        json response;
        response["request_id"] = request_id;
        response["config_reply"] = {
            {"current", runtime_config_to_json(next.runtime)},
            {"applied", true},
        };
        return response.dump();
      }
    }

    if (request.contains("recording_list_request")) {
      const json &list_request = request.at("recording_list_request");
      const std::uint32_t limit = list_request.value("limit", 20U);
      const std::uint64_t newer_than_ms = list_request.value("newer_than_ms", 0ULL);
      const auto recordings =
          recording_catalog_.list(runtime_state_.config_snapshot(), limit, newer_than_ms);

      json files = json::array();
      for (const auto &recording : recordings) {
        files.push_back(recording_file_to_json(recording));
      }

      json response;
      response["request_id"] = request_id;
      response["recording_list_reply"] = {{"files", std::move(files)}};
      return response.dump();
    }

    if (request.contains("event_list_request")) {
      const json &list_request = request.at("event_list_request");
      const std::uint32_t limit = list_request.value("limit", 20U);
      const auto events = runtime_state_.recent_events(limit);

      json payload = json::array();
      for (const auto &event : events) {
        payload.push_back(event_message_to_json(event));
      }

      json response;
      response["request_id"] = request_id;
      response["event_list_reply"] = {{"events", std::move(payload)}};
      return response.dump();
    }

    return error_reply(request_id, 400, "unsupported request payload").dump();
  } catch (const std::exception &error) {
    return error_reply(request_id, 500, error.what()).dump();
  }
}

}  // namespace seccam::core
