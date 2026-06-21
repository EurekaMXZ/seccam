#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ini.h"

#include "cvi_ae.h"
#include "cvi_awb.h"
#include "cvi_buffer.h"
#include "cvi_isp.h"
#include "cvi_rtsp/rtsp.h"
#include "cvi_sns_ctrl.h"
#include "cvi_sys.h"
#include "cvi_tdl.h"
#include "core/cvi_tdl_types_mem.h"
#include "service/cvi_tdl_service.h"
#include "cvi_vi.h"
#include "cvi_vb.h"
#include "cvi_venc.h"
#include "cvi_vpss.h"
#include "seccam/core/core_service.hpp"
#include "seccam/core/detection_tracker.hpp"
#include "seccam/core/recording_engine.hpp"
#include "seccam/core/runtime_state.hpp"

namespace seccam::core {
namespace {

constexpr std::uint64_t kDetectionStaleMs = 1500;
constexpr VI_DEV kViDevice = 0;
constexpr VI_PIPE kViPipe = 0;
constexpr VI_CHN kViChannel = 0;
constexpr VPSS_GRP kVpssGroup = 0;
constexpr VPSS_CHN kStreamChannel = VPSS_CHN0;
constexpr VPSS_CHN kDetectChannel = VPSS_CHN1;
constexpr VENC_CHN kVencChannel = 0;
constexpr std::uint32_t kSensorCount = 1;

struct SensorConfig {
  std::string name;
  int bus_id = 3;
  int sns_i2c_addr = -1;
  int mipi_dev = 0xFF;
  std::array<CVI_S16, 5> lane_id = {-1, -1, -1, -1, -1};
  std::array<CVI_S8, 5> pn_swap = {0, 0, 0, 0, 0};
  CVI_U8 hw_sync = 0;
  CVI_U8 orientation = 0;
};

struct SensorProfile {
  const char *sensor_name = nullptr;
  ISP_SNS_OBJ_S *sensor_object = nullptr;
  SIZE_S input_size = {};
  float frame_rate = 30.0f;
  BAYER_FORMAT_E vi_bayer_format = BAYER_FORMAT_BG;
  ISP_BAYER_FORMAT_E isp_bayer = BAYER_BGGR;
};

struct SensorIniContext {
  SensorConfig config;
  int dev_num = 1;
};

struct DetectionPublishResult {
  cvtdl_object_t objects = {};
  bool trigger_present = false;
};

std::string normalize_sensor_name(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string format_cvi_error(const char *operation, int code) {
  std::ostringstream stream;
  stream << operation << " failed with 0x" << std::hex << code;
  return stream.str();
}

void throw_on_cvi_error(const char *operation, int code) {
  if (code != CVI_SUCCESS) {
    throw std::runtime_error(format_cvi_error(operation, code));
  }
}

void throw_on_rtsp_error(const char *operation, int code) {
  if (code != 0) {
    std::ostringstream stream;
    stream << operation << " failed with " << code;
    throw std::runtime_error(stream.str());
  }
}

int parse_csv_i16(std::array<CVI_S16, 5> &output, const char *value) {
  if (value == nullptr) {
    return 1;
  }

  std::array<CVI_S16, 5> parsed = {-1, -1, -1, -1, -1};
  const std::string raw(value);
  std::size_t start = 0;
  std::size_t index = 0;
  while (start < raw.size() && index < parsed.size()) {
    const std::size_t end = raw.find(',', start);
    const std::string token = raw.substr(start, end == std::string::npos ? std::string::npos
                                                                          : end - start);
    parsed[index++] = static_cast<CVI_S16>(std::atoi(token.c_str()));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  output = parsed;
  return 1;
}

int parse_csv_i8(std::array<CVI_S8, 5> &output, const char *value) {
  if (value == nullptr) {
    return 1;
  }

  std::array<CVI_S8, 5> parsed = {0, 0, 0, 0, 0};
  const std::string raw(value);
  std::size_t start = 0;
  std::size_t index = 0;
  while (start < raw.size() && index < parsed.size()) {
    const std::size_t end = raw.find(',', start);
    const std::string token = raw.substr(start, end == std::string::npos ? std::string::npos
                                                                          : end - start);
    parsed[index++] = static_cast<CVI_S8>(std::atoi(token.c_str()));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  output = parsed;
  return 1;
}

int sensor_ini_handler(void *user, const char *section, const char *name, const char *value) {
  auto *context = static_cast<SensorIniContext *>(user);
  const std::string section_name = section != nullptr ? section : "";
  const std::string key_name = name != nullptr ? name : "";
  const std::string raw_value = value != nullptr ? value : "";

  if (section_name == "source") {
    if (key_name == "dev_num") {
      context->dev_num = std::max(1, std::atoi(raw_value.c_str()));
    }
    return 1;
  }

  if (section_name != "sensor") {
    return 1;
  }

  if (key_name == "name") {
    context->config.name = raw_value;
  } else if (key_name == "bus_id") {
    context->config.bus_id = std::atoi(raw_value.c_str());
  } else if (key_name == "sns_i2c_addr") {
    context->config.sns_i2c_addr = std::atoi(raw_value.c_str());
  } else if (key_name == "mipi_dev") {
    context->config.mipi_dev = std::atoi(raw_value.c_str());
  } else if (key_name == "lane_id") {
    parse_csv_i16(context->config.lane_id, raw_value.c_str());
  } else if (key_name == "pn_swap") {
    parse_csv_i8(context->config.pn_swap, raw_value.c_str());
  } else if (key_name == "hw_sync") {
    context->config.hw_sync = static_cast<CVI_U8>(std::atoi(raw_value.c_str()));
  } else if (key_name == "orien") {
    context->config.orientation = static_cast<CVI_U8>(std::atoi(raw_value.c_str()));
  }

  return 1;
}

SensorConfig load_sensor_config_or_throw(const RuntimeConfig &runtime_config) {
  static const std::array<const char *, 2> kFallbackPaths = {
      "/mnt/data/sensor_cfg.ini",
      "/mnt/system/usr/bin/sensor_cfg.ini",
  };

  SensorIniContext context;
  std::vector<std::string> candidates;
  if (!runtime_config.sensor_config_path.empty()) {
    candidates.push_back(runtime_config.sensor_config_path);
  }
  for (const char *fallback : kFallbackPaths) {
    if (std::find(candidates.begin(), candidates.end(), std::string(fallback)) == candidates.end()) {
      candidates.emplace_back(fallback);
    }
  }

  for (const std::string &path : candidates) {
    const int parse_result = ini_parse(path.c_str(), sensor_ini_handler, &context);
    if (parse_result == 0 && !context.config.name.empty()) {
      return context.config;
    }
  }

  throw std::runtime_error("unable to load sensor_cfg.ini for seccam-core");
}

const SensorProfile *match_sensor_profile(const SensorConfig &sensor_config) {
  static SensorProfile gc2083 = [] {
    SensorProfile profile;
    profile.sensor_name = "gcore_gc2083_mipi_2m_30fps_10bit";
    profile.sensor_object = &stSnsGc2083_Obj;
    profile.input_size.u32Width = 1920;
    profile.input_size.u32Height = 1080;
    profile.frame_rate = 30.0f;
    profile.vi_bayer_format = BAYER_FORMAT_RG;
    profile.isp_bayer = BAYER_RGGB;
    return profile;
  }();
  static SensorProfile gc4653 = [] {
    SensorProfile profile;
    profile.sensor_name = "gcore_gc4653_mipi_4m_30fps_10bit";
    profile.sensor_object = &stSnsGc4653_Obj;
    profile.input_size.u32Width = 2560;
    profile.input_size.u32Height = 1440;
    profile.frame_rate = 30.0f;
    profile.vi_bayer_format = BAYER_FORMAT_GR;
    profile.isp_bayer = BAYER_GRBG;
    return profile;
  }();
  static SensorProfile ov5647 = [] {
    SensorProfile profile;
    profile.sensor_name = "ov_ov5647_mipi_2m_30fps_10bit";
    profile.sensor_object = &stSnsOv5647_Obj;
    profile.input_size.u32Width = 1920;
    profile.input_size.u32Height = 1080;
    profile.frame_rate = 30.0f;
    profile.vi_bayer_format = BAYER_FORMAT_BG;
    profile.isp_bayer = BAYER_BGGR;
    return profile;
  }();

  const std::string sensor_name = normalize_sensor_name(sensor_config.name);
  if (sensor_name == "gc2083" || sensor_name == normalize_sensor_name(gc2083.sensor_name)) {
    return &gc2083;
  }
  if (sensor_name == "gc4653" || sensor_name == normalize_sensor_name(gc4653.sensor_name)) {
    return &gc4653;
  }
  if (sensor_name == "ov5647" || sensor_name == normalize_sensor_name(ov5647.sensor_name)) {
    return &ov5647;
  }
  return nullptr;
}

VI_DEV_ATTR_S make_vi_dev_attr(const SensorProfile &profile) {
  VI_DEV_ATTR_S attr = {
      VI_MODE_MIPI,
      VI_WORK_MODE_1Multiplex,
      VI_SCAN_PROGRESSIVE,
      {-1, -1, -1, -1},
      VI_DATA_SEQ_YUYV,
      {
          VI_VSYNC_PULSE,
          VI_VSYNC_NEG_LOW,
          VI_HSYNC_VALID_SINGNAL,
          VI_HSYNC_NEG_HIGH,
          VI_VSYNC_VALID_SIGNAL,
          VI_VSYNC_VALID_NEG_HIGH,
          {0, profile.input_size.u32Width, 0, 0, profile.input_size.u32Height, 0, 0, 0, 0},
      },
      VI_DATA_TYPE_RGB,
      {profile.input_size.u32Width, profile.input_size.u32Height},
      {WDR_MODE_NONE, profile.input_size.u32Height, 0},
      profile.vi_bayer_format,
  };
  attr.stWDRAttr.enWDRMode = WDR_MODE_NONE;
  attr.snrFps = static_cast<CVI_U32>(profile.frame_rate);
  return attr;
}

ISP_PUB_ATTR_S make_isp_pub_attr(const SensorProfile &profile) {
  ISP_PUB_ATTR_S attr = {
      {0, 0, profile.input_size.u32Width, profile.input_size.u32Height},
      {profile.input_size.u32Width, profile.input_size.u32Height},
      profile.frame_rate,
      profile.isp_bayer,
      WDR_MODE_NONE,
      0,
  };
  return attr;
}

VI_CHN_ATTR_S make_vi_chn_attr(const SensorProfile &profile, CVI_U8 orientation) {
  VI_CHN_ATTR_S attr = {};
  attr.stSize.u32Width = profile.input_size.u32Width;
  attr.stSize.u32Height = profile.input_size.u32Height;
  attr.enPixelFormat = VI_PIXEL_FORMAT;
  attr.enDynamicRange = DYNAMIC_RANGE_SDR8;
  attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
  attr.enCompressMode = COMPRESS_MODE_NONE;
  attr.bMirror = CVI_FALSE;
  attr.bFlip = CVI_FALSE;
  attr.u32Depth = 0;
  attr.stFrameRate.s32SrcFrameRate = -1;
  attr.stFrameRate.s32DstFrameRate = -1;
  attr.u32BindVbPool = 0;

  if (orientation <= 3) {
    attr.bMirror = orientation & 0x1;
    attr.bFlip = orientation & 0x2;
  }
  return attr;
}

VPSS_GRP_ATTR_S make_vpss_group_attr(const SensorProfile &profile) {
  VPSS_GRP_ATTR_S attr = {};
  attr.stFrameRate.s32SrcFrameRate = -1;
  attr.stFrameRate.s32DstFrameRate = -1;
  attr.enPixelFormat = VI_PIXEL_FORMAT;
  attr.u32MaxW = profile.input_size.u32Width;
  attr.u32MaxH = profile.input_size.u32Height;
  attr.u8VpssDev = 1;
  return attr;
}

VPSS_CHN_ATTR_S make_vpss_channel_attr(std::uint32_t width, std::uint32_t height,
                                       PIXEL_FORMAT_E pixel_format) {
  VPSS_CHN_ATTR_S attr = {};
  attr.u32Width = width;
  attr.u32Height = height;
  attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
  attr.enPixelFormat = pixel_format;
  attr.stFrameRate.s32SrcFrameRate = -1;
  attr.stFrameRate.s32DstFrameRate = -1;
  attr.u32Depth = 1;
  attr.bMirror = CVI_FALSE;
  attr.bFlip = CVI_FALSE;
  attr.stAspectRatio.enMode = ASPECT_RATIO_AUTO;
  attr.stAspectRatio.u32BgColor = RGB_8BIT(0, 0, 0);
  attr.stNormalize.bEnable = CVI_FALSE;
  attr.stNormalize.rounding = VPSS_ROUNDING_TO_EVEN;
  return attr;
}

VENC_CHN_ATTR_S make_venc_attr(const RuntimeConfig &runtime_config) {
  VENC_CHN_ATTR_S attr = {};
  attr.stVencAttr.enType = PT_H264;
  attr.stVencAttr.u32MaxPicWidth = runtime_config.stream_width;
  attr.stVencAttr.u32MaxPicHeight = runtime_config.stream_height;
  attr.stVencAttr.u32PicWidth = runtime_config.stream_width;
  attr.stVencAttr.u32PicHeight = runtime_config.stream_height;
  attr.stVencAttr.u32BufSize =
      std::max<CVI_U32>(0x30000, runtime_config.stream_width * runtime_config.stream_height);
  attr.stVencAttr.bByFrame = CVI_TRUE;
  attr.stVencAttr.bEsBufQueueEn = CVI_TRUE;
  attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
  attr.stGopAttr.stNormalP.s32IPQpDelta = 2;

  attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
  attr.stRcAttr.stH264Cbr.u32Gop = 30;
  attr.stRcAttr.stH264Cbr.u32StatTime = 0;
  attr.stRcAttr.stH264Cbr.u32SrcFrameRate = 30;
  attr.stRcAttr.stH264Cbr.fr32DstFrameRate = 30;
  attr.stRcAttr.stH264Cbr.bVariFpsEn = 0;
  attr.stRcAttr.stH264Cbr.u32BitRate = runtime_config.bitrate_kbps;
  return attr;
}

CVI_TDL_SUPPORTED_MODEL_E resolve_model_id_or_throw(const std::string &model_name,
                                                    int *default_person_class_id) {
  struct ModelEntry {
    const char *name;
    CVI_TDL_SUPPORTED_MODEL_E model_id;
    int default_person_class_id;
  };
  static const std::array<ModelEntry, 12> kModels = {{
      {"yolo", CVI_TDL_SUPPORTED_MODEL_YOLO, 0},
      {"yolov3", CVI_TDL_SUPPORTED_MODEL_YOLOV3, 0},
      {"yolov5", CVI_TDL_SUPPORTED_MODEL_YOLOV5, 0},
      {"yolov6", CVI_TDL_SUPPORTED_MODEL_YOLOV6, 0},
      {"yolov7", CVI_TDL_SUPPORTED_MODEL_YOLOV7, 0},
      {"yolox", CVI_TDL_SUPPORTED_MODEL_YOLOX, 0},
      {"ppyoloe", CVI_TDL_SUPPORTED_MODEL_PPYOLOE, 0},
      {"yolov8-detection", CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, 0},
      {"yolov8-person-pets", CVI_TDL_SUPPORTED_MODEL_PERSON_PETS_DETECTION, 2},
      {"mobiledetv2-pedestrian", CVI_TDL_SUPPORTED_MODEL_MOBILEDETV2_PEDESTRIAN, 0},
      {"mobiledetv2-person-pets", CVI_TDL_SUPPORTED_MODEL_MOBILEDETV2_PERSON_PETS, 0},
      {"mobiledetv2-coco80", CVI_TDL_SUPPORTED_MODEL_MOBILEDETV2_COCO80, 0},
  }};

  for (const auto &entry : kModels) {
    if (model_name == entry.name) {
      if (default_person_class_id != nullptr) {
        *default_person_class_id = entry.default_person_class_id;
      }
      return entry.model_id;
    }
  }

  throw std::runtime_error("unsupported model_name for seccam-core: " + model_name);
}

bool object_is_person(int person_class_id, const cvtdl_object_info_t &object_info) {
  if (object_info.classes == person_class_id) {
    return true;
  }
  return std::strcmp(object_info.name, "person") == 0;
}

bool object_is_person_pet(const cvtdl_object_info_t &object_info) {
  return object_info.classes >= 0 && object_info.classes <= 2;
}

const char *person_pet_label(int class_id) {
  switch (class_id) {
    case 0:
      return "cat";
    case 1:
      return "dog";
    case 2:
      return "person";
    default:
      return "unknown";
  }
}

void rescale_detection_boxes_for_display(cvtdl_object_t *objects, std::uint32_t fallback_width,
                                         std::uint32_t fallback_height,
                                         const VIDEO_FRAME_INFO_S *display_frame) {
  const std::uint32_t source_width = objects->width > 0 ? objects->width : fallback_width;
  const std::uint32_t source_height = objects->height > 0 ? objects->height : fallback_height;
  const std::uint32_t target_width = display_frame->stVFrame.u32Width;
  const std::uint32_t target_height = display_frame->stVFrame.u32Height;

  if (source_width == 0 || source_height == 0 || target_width == 0 || target_height == 0) {
    return;
  }
  if (source_width == target_width && source_height == target_height) {
    return;
  }

  const float scale_x = static_cast<float>(target_width) / static_cast<float>(source_width);
  const float scale_y = static_cast<float>(target_height) / static_cast<float>(source_height);
  const float max_x = static_cast<float>(target_width - 1U);
  const float max_y = static_cast<float>(target_height - 1U);

  for (uint32_t index = 0; index < objects->size; ++index) {
    cvtdl_bbox_t *bbox = &objects->info[index].bbox;
    bbox->x1 = std::clamp(bbox->x1 * scale_x, 0.0f, max_x);
    bbox->x2 = std::clamp(bbox->x2 * scale_x, 0.0f, max_x);
    bbox->y1 = std::clamp(bbox->y1 * scale_y, 0.0f, max_y);
    bbox->y2 = std::clamp(bbox->y2 * scale_y, 0.0f, max_y);
  }

  objects->width = target_width;
  objects->height = target_height;
}

class CviBackend final : public CoreService::Backend {
 public:
  explicit CviBackend(RuntimeState &runtime_state) : runtime_state_(runtime_state) {}

  ~CviBackend() override {
    try {
      stop();
    } catch (...) {
    }
  }

  void start(const RuntimeConfig &config) override {
    runtime_config_ = config;
    sensor_config_ = load_sensor_config_or_throw(runtime_config_);
    sensor_profile_ = match_sensor_profile(sensor_config_);
    if (sensor_profile_ == nullptr) {
      throw std::runtime_error("unsupported sensor in sensor_cfg.ini: " + sensor_config_.name);
    }

    detection_tracker_ =
        std::make_unique<DetectionTracker>(make_detection_tracker_config(runtime_config_),
                                           &runtime_state_);
    recording_engine_ =
        std::make_unique<RecordingEngine>(make_recording_engine_config(runtime_config_),
                                          &runtime_state_);

    try {
      initialize_media_stack();
      initialize_inference();

      stop_requested_.store(false);
      detection_thread_ = std::thread(&CviBackend::run_detection_loop, this);
      encode_thread_ = std::thread(&CviBackend::run_encode_loop, this);
      started_ = true;
      runtime_state_.mark_components_ready(true, true);
    } catch (...) {
      stop_requested_.store(true);
      join_threads();
      destroy_inference();
      shutdown_media_stack();
      detection_tracker_.reset();
      recording_engine_.reset();
      runtime_state_.mark_components_ready(false, false);
      runtime_state_.set_pipeline_counters(0, 0);
      throw;
    }
  }

  void stop() override {
    if (!started_ && !sys_initialized_) {
      return;
    }

    stop_requested_.store(true);
    join_threads();
    destroy_inference();
    shutdown_media_stack();

    {
      std::lock_guard<std::mutex> lock(draw_objects_mutex_);
      CVI_TDL_Free(&draw_objects_);
      std::memset(&draw_objects_, 0, sizeof(draw_objects_));
    }

    detection_tracker_.reset();
    recording_engine_.reset();
    started_ = false;
  }

 private:
  void initialize_media_stack() {
    initialize_sys_and_vb();
    initialize_sensor_and_vi();
    initialize_vpss();
    initialize_venc();
    initialize_rtsp();
  }

  void initialize_sys_and_vb() {
    VB_CONFIG_S vb_config = {};
    vb_config.u32MaxPoolCnt = 3;

    vb_config.astCommPool[0].u32BlkSize =
        COMMON_GetPicBufferSize(sensor_profile_->input_size.u32Width,
                                sensor_profile_->input_size.u32Height, VI_PIXEL_FORMAT,
                                DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
    vb_config.astCommPool[0].u32BlkCnt = 3;

    vb_config.astCommPool[1].u32BlkSize =
        COMMON_GetPicBufferSize(runtime_config_.detect_width, runtime_config_.detect_height,
                                VI_PIXEL_FORMAT, DATA_BITWIDTH_8, COMPRESS_MODE_NONE,
                                DEFAULT_ALIGN);
    vb_config.astCommPool[1].u32BlkCnt = 3;

    vb_config.astCommPool[2].u32BlkSize =
        COMMON_GetPicBufferSize(runtime_config_.tdl_vb_width, runtime_config_.tdl_vb_height,
                                PIXEL_FORMAT_RGB_888_PLANAR, DATA_BITWIDTH_8, COMPRESS_MODE_NONE,
                                DEFAULT_ALIGN);
    vb_config.astCommPool[2].u32BlkCnt = 1;

    throw_on_cvi_error("CVI_VB_SetConfig", CVI_VB_SetConfig(&vb_config));
    throw_on_cvi_error("CVI_VB_Init", CVI_VB_Init());
    throw_on_cvi_error("CVI_SYS_Init", CVI_SYS_Init());
    sys_initialized_ = true;

    VI_VPSS_MODE_S vi_vpss_mode = {};
    vi_vpss_mode.aenMode[0] = VI_OFFLINE_VPSS_ONLINE;
    vi_vpss_mode.aenMode[1] = VI_OFFLINE_VPSS_ONLINE;
    throw_on_cvi_error("CVI_SYS_SetVIVPSSMode", CVI_SYS_SetVIVPSSMode(&vi_vpss_mode));

    VPSS_MODE_S vpss_mode = {};
    vpss_mode.enMode = VPSS_MODE_DUAL;
    vpss_mode.aenInput[0] = VPSS_INPUT_MEM;
    vpss_mode.ViPipe[0] = kViPipe;
    vpss_mode.aenInput[1] = VPSS_INPUT_ISP;
    vpss_mode.ViPipe[1] = kViPipe;
    throw_on_cvi_error("CVI_SYS_SetVPSSModeEx", CVI_SYS_SetVPSSModeEx(&vpss_mode));

    throw_on_cvi_error("CVI_SYS_VI_Open", CVI_SYS_VI_Open());
    vi_opened_ = true;
  }

  void initialize_sensor_and_vi() {
    throw_on_cvi_error("CVI_VI_SetDevNum", CVI_VI_SetDevNum(kSensorCount));
    throw_on_cvi_error("CVI_AE_Register", register_ae_awb_libs());
    ae_awb_registered_ = true;

    register_sensor_driver();
    sensor_registered_ = true;

    configure_sensor_mode();
    start_mipi();
    probe_sensor();

    VI_DEV_ATTR_S dev_attr = make_vi_dev_attr(*sensor_profile_);
    throw_on_cvi_error("CVI_VI_SetDevAttr", CVI_VI_SetDevAttr(kViDevice, &dev_attr));
    throw_on_cvi_error("CVI_VI_EnableDev", CVI_VI_EnableDev(kViDevice));
    vi_device_enabled_ = true;

    VI_DEV_BIND_PIPE_S bind_pipe = {};
    bind_pipe.u32Num = 1;
    bind_pipe.PipeId[0] = kViPipe;
    throw_on_cvi_error("CVI_VI_SetDevBindPipe", CVI_VI_SetDevBindPipe(kViDevice, &bind_pipe));

    VI_PIPE_ATTR_S pipe_attr = {};
    pipe_attr.bYuvSkip = CVI_FALSE;
    pipe_attr.u32MaxW = sensor_profile_->input_size.u32Width;
    pipe_attr.u32MaxH = sensor_profile_->input_size.u32Height;
    pipe_attr.enPixFmt = PIXEL_FORMAT_RGB_BAYER_12BPP;
    pipe_attr.enBitWidth = DATA_BITWIDTH_12;
    pipe_attr.stFrameRate.s32SrcFrameRate = -1;
    pipe_attr.stFrameRate.s32DstFrameRate = -1;
    pipe_attr.bNrEn = CVI_TRUE;
    pipe_attr.bYuvBypassPath = CVI_FALSE;
    pipe_attr.enCompressMode = COMPRESS_MODE_NONE;
    throw_on_cvi_error("CVI_VI_CreatePipe", CVI_VI_CreatePipe(kViPipe, &pipe_attr));
    vi_pipe_created_ = true;
    throw_on_cvi_error("CVI_VI_StartPipe", CVI_VI_StartPipe(kViPipe));
    vi_pipe_started_ = true;

    ISP_BIND_ATTR_S bind_attr = {};
    std::snprintf(bind_attr.stAeLib.acLibName, sizeof(bind_attr.stAeLib.acLibName), "%s",
                  CVI_AE_LIB_NAME);
    bind_attr.stAeLib.s32Id = kViPipe;
    bind_attr.sensorId = 0;
    std::snprintf(bind_attr.stAwbLib.acLibName, sizeof(bind_attr.stAwbLib.acLibName), "%s",
                  CVI_AWB_LIB_NAME);
    bind_attr.stAwbLib.s32Id = kViPipe;
    throw_on_cvi_error("CVI_ISP_SetBindAttr", CVI_ISP_SetBindAttr(kViPipe, &bind_attr));
    throw_on_cvi_error("CVI_ISP_MemInit", CVI_ISP_MemInit(kViPipe));

    ISP_PUB_ATTR_S pub_attr = make_isp_pub_attr(*sensor_profile_);
    throw_on_cvi_error("CVI_ISP_SetPubAttr", CVI_ISP_SetPubAttr(kViPipe, &pub_attr));
    throw_on_cvi_error("CVI_ISP_Init", CVI_ISP_Init(kViPipe));
    isp_initialized_ = true;

    stop_requested_.store(false);
    isp_thread_ = std::thread([this]() {
      const int ret = CVI_ISP_Run(kViPipe);
      if (!stop_requested_.load() && ret != CVI_SUCCESS) {
        runtime_state_.record_fault("isp", format_cvi_error("CVI_ISP_Run", ret));
      }
    });

    VI_CHN_ATTR_S chn_attr = make_vi_chn_attr(*sensor_profile_, sensor_config_.orientation);
    throw_on_cvi_error("CVI_VI_SetChnAttr", CVI_VI_SetChnAttr(kViPipe, kViChannel, &chn_attr));
    throw_on_cvi_error("CVI_VI_EnableChn", CVI_VI_EnableChn(kViPipe, kViChannel));
    vi_channel_enabled_ = true;
  }

  void initialize_vpss() {
    VPSS_GRP_ATTR_S group_attr = make_vpss_group_attr(*sensor_profile_);
    throw_on_cvi_error("CVI_VPSS_CreateGrp", CVI_VPSS_CreateGrp(kVpssGroup, &group_attr));
    vpss_group_created_ = true;
    throw_on_cvi_error("CVI_VPSS_ResetGrp", CVI_VPSS_ResetGrp(kVpssGroup));

    VPSS_CHN_ATTR_S stream_attr =
        make_vpss_channel_attr(runtime_config_.stream_width, runtime_config_.stream_height,
                               VI_PIXEL_FORMAT);
    VPSS_CHN_ATTR_S detect_attr =
        make_vpss_channel_attr(runtime_config_.detect_width, runtime_config_.detect_height,
                               VI_PIXEL_FORMAT);

    throw_on_cvi_error("CVI_VPSS_SetChnAttr",
                       CVI_VPSS_SetChnAttr(kVpssGroup, kStreamChannel, &stream_attr));
    throw_on_cvi_error("CVI_VPSS_EnableChn", CVI_VPSS_EnableChn(kVpssGroup, kStreamChannel));
    stream_channel_enabled_ = true;

    throw_on_cvi_error("CVI_VPSS_SetChnAttr",
                       CVI_VPSS_SetChnAttr(kVpssGroup, kDetectChannel, &detect_attr));
    throw_on_cvi_error("CVI_VPSS_EnableChn", CVI_VPSS_EnableChn(kVpssGroup, kDetectChannel));
    detect_channel_enabled_ = true;

    throw_on_cvi_error("CVI_VPSS_StartGrp", CVI_VPSS_StartGrp(kVpssGroup));
    vpss_group_started_ = true;

    throw_on_cvi_error("CVI_VPSS_AttachVbPool",
                       CVI_VPSS_AttachVbPool(kVpssGroup, kStreamChannel, static_cast<VB_POOL>(0)));
    stream_pool_attached_ = true;
    throw_on_cvi_error("CVI_VPSS_AttachVbPool",
                       CVI_VPSS_AttachVbPool(kVpssGroup, kDetectChannel, static_cast<VB_POOL>(1)));
    detect_pool_attached_ = true;
  }

  void initialize_venc() {
    VENC_PARAM_MOD_S mod_param = {};
    throw_on_cvi_error("CVI_VENC_GetModParam", CVI_VENC_GetModParam(&mod_param));
    mod_param.stH264eModParam.enH264eVBSource = VB_SOURCE_COMMON;
    mod_param.stH264eModParam.bSingleEsBuf = true;
    mod_param.stH264eModParam.u32SingleEsBufSize = 0x30000;
    mod_param.stH264eModParam.u32UserDataMaxLen = 3072;
    throw_on_cvi_error("CVI_VENC_SetModParam", CVI_VENC_SetModParam(&mod_param));

    VENC_CHN_ATTR_S venc_attr = make_venc_attr(runtime_config_);
    throw_on_cvi_error("CVI_VENC_CreateChn", CVI_VENC_CreateChn(kVencChannel, &venc_attr));
    venc_created_ = true;

    VENC_RECV_PIC_PARAM_S recv_param = {};
    recv_param.s32RecvPicNum = -1;
    throw_on_cvi_error("CVI_VENC_StartRecvFrame",
                       CVI_VENC_StartRecvFrame(kVencChannel, &recv_param));
    venc_started_ = true;
  }

  void initialize_rtsp() {
    CVI_RTSP_CONFIG rtsp_config = {};
    rtsp_config.port = static_cast<int>(runtime_config_.rtsp_port);
    rtsp_config.maxConnNum = 8;
    rtsp_config.timeout = 30;
    rtsp_config.packetLen = 1400;
    rtsp_config.tcpBufSize = 1024 * 1024;
    throw_on_rtsp_error("CVI_RTSP_Create", CVI_RTSP_Create(&rtsp_ctx_, &rtsp_config));

    CVI_RTSP_SESSION_ATTR session_attr = {};
    std::snprintf(session_attr.name, sizeof(session_attr.name), "%s",
                  runtime_config_.rtsp_stream_name.c_str());
    session_attr.video.codec = RTSP_VIDEO_H264;
    session_attr.video.bitrate = runtime_config_.bitrate_kbps;
    throw_on_rtsp_error("CVI_RTSP_CreateSession",
                        CVI_RTSP_CreateSession(rtsp_ctx_, &session_attr, &rtsp_session_));

    throw_on_rtsp_error("CVI_RTSP_Start", CVI_RTSP_Start(rtsp_ctx_));
    rtsp_started_ = true;
  }

  int register_ae_awb_libs() {
    ALG_LIB_S ae_lib = {};
    ae_lib.s32Id = kViPipe;
    std::snprintf(ae_lib.acLibName, sizeof(ae_lib.acLibName), "%s", CVI_AE_LIB_NAME);
    throw_on_cvi_error("CVI_AE_Register", CVI_AE_Register(kViPipe, &ae_lib));

    ALG_LIB_S awb_lib = {};
    awb_lib.s32Id = kViPipe;
    std::snprintf(awb_lib.acLibName, sizeof(awb_lib.acLibName), "%s", CVI_AWB_LIB_NAME);
    throw_on_cvi_error("CVI_AWB_Register", CVI_AWB_Register(kViPipe, &awb_lib));
    return CVI_SUCCESS;
  }

  void register_sensor_driver() {
    if (sensor_profile_->sensor_object == nullptr) {
      throw std::runtime_error("sensor driver object is null");
    }

    ISP_INIT_ATTR_S init_attr = {};
    init_attr.u16UseHwSync = sensor_config_.hw_sync;
    init_attr.enGainMode = SNS_GAIN_MODE_SHARE;
    init_attr.enL2SMode = SNS_L2S_MODE_AUTO;
    init_attr.enSnsBdgMuxMode = SNS_BDG_MUX_NONE;

    if (sensor_profile_->sensor_object->pfnSetInit != nullptr) {
      throw_on_cvi_error("sensor->pfnSetInit",
                         sensor_profile_->sensor_object->pfnSetInit(kViPipe, &init_attr));
    }

    RX_INIT_ATTR_S rx_init_attr = {};
    rx_init_attr.MipiDev = static_cast<CVI_U32>(sensor_config_.mipi_dev);
    std::copy(sensor_config_.lane_id.begin(), sensor_config_.lane_id.end(), rx_init_attr.as16LaneId);
    std::copy(sensor_config_.pn_swap.begin(), sensor_config_.pn_swap.end(), rx_init_attr.as8PNSwap);
    if (sensor_profile_->sensor_object->pfnPatchRxAttr != nullptr) {
      throw_on_cvi_error("sensor->pfnPatchRxAttr",
                         sensor_profile_->sensor_object->pfnPatchRxAttr(&rx_init_attr));
    }

    ISP_SNS_COMMBUS_U bus_info = {};
    bus_info.s8I2cDev = static_cast<CVI_S8>(sensor_config_.bus_id);
    throw_on_cvi_error("sensor->pfnSetBusInfo",
                       sensor_profile_->sensor_object->pfnSetBusInfo(kViPipe, bus_info));
    if (sensor_profile_->sensor_object->pfnPatchI2cAddr != nullptr) {
      sensor_profile_->sensor_object->pfnPatchI2cAddr(sensor_config_.sns_i2c_addr);
    }

    ALG_LIB_S ae_lib = {};
    ae_lib.s32Id = kViPipe;
    std::snprintf(ae_lib.acLibName, sizeof(ae_lib.acLibName), "%s", CVI_AE_LIB_NAME);
    ALG_LIB_S awb_lib = {};
    awb_lib.s32Id = kViPipe;
    std::snprintf(awb_lib.acLibName, sizeof(awb_lib.acLibName), "%s", CVI_AWB_LIB_NAME);
    throw_on_cvi_error("sensor->pfnRegisterCallback",
                       sensor_profile_->sensor_object->pfnRegisterCallback(kViPipe, &ae_lib,
                                                                           &awb_lib));
  }

  void configure_sensor_mode() {
    ISP_SENSOR_EXP_FUNC_S sensor_func = {};
    throw_on_cvi_error("sensor->pfnExpSensorCb",
                       sensor_profile_->sensor_object->pfnExpSensorCb(&sensor_func));

    ISP_CMOS_SENSOR_IMAGE_MODE_S sensor_mode = {};
    sensor_mode.u16Width = sensor_profile_->input_size.u32Width;
    sensor_mode.u16Height = sensor_profile_->input_size.u32Height;
    sensor_mode.f32Fps = sensor_profile_->frame_rate;

    if (sensor_func.pfn_cmos_set_image_mode != nullptr) {
      throw_on_cvi_error("sensor->pfn_cmos_set_image_mode",
                         sensor_func.pfn_cmos_set_image_mode(kViPipe, &sensor_mode));
    }
    if (sensor_func.pfn_cmos_set_wdr_mode != nullptr) {
      throw_on_cvi_error("sensor->pfn_cmos_set_wdr_mode",
                         sensor_func.pfn_cmos_set_wdr_mode(kViPipe, WDR_MODE_NONE));
    }
  }

  void start_mipi() {
    SNS_COMBO_DEV_ATTR_S rx_attr = {};
    throw_on_cvi_error("sensor->pfnGetRxAttr",
                       sensor_profile_->sensor_object->pfnGetRxAttr(kViPipe, &rx_attr));
    if (sensor_config_.mipi_dev == 0xFF) {
      sensor_config_.mipi_dev = rx_attr.devno;
    }

    throw_on_cvi_error("CVI_MIPI_SetSensorReset",
                       CVI_MIPI_SetSensorReset(sensor_config_.mipi_dev, 1));
    throw_on_cvi_error("CVI_MIPI_SetMipiReset",
                       CVI_MIPI_SetMipiReset(sensor_config_.mipi_dev, 1));
    throw_on_cvi_error("CVI_MIPI_SetMipiAttr", CVI_MIPI_SetMipiAttr(kViPipe, &rx_attr));
    throw_on_cvi_error("CVI_MIPI_SetSensorClock",
                       CVI_MIPI_SetSensorClock(sensor_config_.mipi_dev, 1));
    std::this_thread::sleep_for(std::chrono::microseconds(20));
    throw_on_cvi_error("CVI_MIPI_SetSensorReset",
                       CVI_MIPI_SetSensorReset(sensor_config_.mipi_dev, 0));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  void probe_sensor() {
    if (sensor_profile_->sensor_object->pfnSnsProbe != nullptr) {
      throw_on_cvi_error("sensor->pfnSnsProbe",
                         sensor_profile_->sensor_object->pfnSnsProbe(kViPipe));
    }
  }

  void initialize_inference() {
    int default_person_class_id = -1;
    model_id_ = resolve_model_id_or_throw(runtime_config_.model_name, &default_person_class_id);
    person_class_id_ =
        runtime_config_.person_class_id >= 0 ? runtime_config_.person_class_id : default_person_class_id;

    throw_on_cvi_error("CVI_TDL_CreateHandle2", CVI_TDL_CreateHandle2(&tdl_handle_, 2, 0));
    tdl_handle_created_ = true;
    throw_on_cvi_error("CVI_TDL_SetVBPool", CVI_TDL_SetVBPool(tdl_handle_, 0, 2));
    CVI_TDL_SetVpssTimeout(tdl_handle_, 1000);
    throw_on_cvi_error("CVI_TDL_Service_CreateHandle",
                       CVI_TDL_Service_CreateHandle(&tdl_service_handle_, tdl_handle_));
    tdl_service_created_ = true;
    throw_on_cvi_error("CVI_TDL_OpenModel",
                       CVI_TDL_OpenModel(tdl_handle_, model_id_, runtime_config_.model_path.c_str()));
    const int threshold_ret =
        CVI_TDL_SetModelThreshold(tdl_handle_, model_id_, runtime_config_.threshold);
    if (threshold_ret != CVI_SUCCESS) {
      runtime_state_.record_fault("inference",
                                  format_cvi_error("CVI_TDL_SetModelThreshold", threshold_ret));
    }
  }

  void destroy_inference() {
    if (tdl_service_created_) {
      CVI_TDL_Service_DestroyHandle(tdl_service_handle_);
      tdl_service_handle_ = nullptr;
      tdl_service_created_ = false;
    }
    if (tdl_handle_created_) {
      CVI_TDL_DestroyHandle(tdl_handle_);
      tdl_handle_ = nullptr;
      tdl_handle_created_ = false;
    }
  }

  void shutdown_media_stack() {
    destroy_rtsp();
    destroy_venc();
    destroy_vpss();
    destroy_vi_and_isp();
    destroy_sys_and_vb();
  }

  void destroy_rtsp() {
    if (rtsp_ctx_ == nullptr) {
      return;
    }

    if (rtsp_started_) {
      CVI_RTSP_Stop(rtsp_ctx_);
      rtsp_started_ = false;
    }
    if (rtsp_session_ != nullptr) {
      CVI_RTSP_DestroySession(rtsp_ctx_, rtsp_session_);
      rtsp_session_ = nullptr;
    }
    CVI_RTSP_Destroy(&rtsp_ctx_);
  }

  void destroy_venc() {
    if (venc_started_) {
      CVI_VENC_StopRecvFrame(kVencChannel);
      venc_started_ = false;
    }
    if (venc_created_) {
      CVI_VENC_ResetChn(kVencChannel);
      CVI_VENC_DestroyChn(kVencChannel);
      venc_created_ = false;
    }
  }

  void destroy_vpss() {
    if (stream_pool_attached_) {
      CVI_VPSS_DetachVbPool(kVpssGroup, kStreamChannel);
      stream_pool_attached_ = false;
    }
    if (detect_pool_attached_) {
      CVI_VPSS_DetachVbPool(kVpssGroup, kDetectChannel);
      detect_pool_attached_ = false;
    }
    if (stream_channel_enabled_) {
      CVI_VPSS_DisableChn(kVpssGroup, kStreamChannel);
      stream_channel_enabled_ = false;
    }
    if (detect_channel_enabled_) {
      CVI_VPSS_DisableChn(kVpssGroup, kDetectChannel);
      detect_channel_enabled_ = false;
    }
    if (vpss_group_started_) {
      CVI_VPSS_StopGrp(kVpssGroup);
      vpss_group_started_ = false;
    }
    if (vpss_group_created_) {
      CVI_VPSS_DestroyGrp(kVpssGroup);
      vpss_group_created_ = false;
    }
  }

  void destroy_vi_and_isp() {
    if (vi_channel_enabled_) {
      CVI_VI_DisableChn(kViPipe, kViChannel);
      vi_channel_enabled_ = false;
    }
    if (isp_initialized_) {
      CVI_ISP_Exit(kViPipe);
      isp_initialized_ = false;
    }
    if (isp_thread_.joinable()) {
      isp_thread_.join();
    }

    if (sensor_registered_) {
      ALG_LIB_S ae_lib = {};
      ae_lib.s32Id = kViPipe;
      std::snprintf(ae_lib.acLibName, sizeof(ae_lib.acLibName), "%s", CVI_AE_LIB_NAME);
      ALG_LIB_S awb_lib = {};
      awb_lib.s32Id = kViPipe;
      std::snprintf(awb_lib.acLibName, sizeof(awb_lib.acLibName), "%s", CVI_AWB_LIB_NAME);
      if (sensor_profile_ != nullptr && sensor_profile_->sensor_object != nullptr &&
          sensor_profile_->sensor_object->pfnUnRegisterCallback != nullptr) {
        sensor_profile_->sensor_object->pfnUnRegisterCallback(kViPipe, &ae_lib, &awb_lib);
      }
      sensor_registered_ = false;
    }

    if (ae_awb_registered_) {
      ALG_LIB_S awb_lib = {};
      awb_lib.s32Id = kViPipe;
      std::snprintf(awb_lib.acLibName, sizeof(awb_lib.acLibName), "%s", CVI_AWB_LIB_NAME);
      CVI_AWB_UnRegister(kViPipe, &awb_lib);
      ALG_LIB_S ae_lib = {};
      ae_lib.s32Id = kViPipe;
      std::snprintf(ae_lib.acLibName, sizeof(ae_lib.acLibName), "%s", CVI_AE_LIB_NAME);
      CVI_AE_UnRegister(kViPipe, &ae_lib);
      ae_awb_registered_ = false;
    }

    if (vi_pipe_started_) {
      CVI_VI_StopPipe(kViPipe);
      vi_pipe_started_ = false;
    }
    if (vi_pipe_created_) {
      CVI_VI_DestroyPipe(kViPipe);
      vi_pipe_created_ = false;
    }
    if (vi_device_enabled_) {
      CVI_VI_DisableDev(kViDevice);
      vi_device_enabled_ = false;
    }
  }

  void destroy_sys_and_vb() {
    if (vi_opened_) {
      CVI_SYS_VI_Close();
      vi_opened_ = false;
    }
    if (sys_initialized_) {
      CVI_SYS_Exit();
      CVI_VB_Exit();
      sys_initialized_ = false;
    }
  }

  void join_threads() {
    if (detection_thread_.joinable()) {
      detection_thread_.join();
    }
    if (encode_thread_.joinable()) {
      encode_thread_.join();
    }
    if (isp_thread_.joinable() && !isp_initialized_) {
      isp_thread_.join();
    }
  }

  void run_detection_loop() {
    std::uint32_t frames = 0;
    std::uint64_t window_start_ms = 0;

    while (!stop_requested_.load()) {
      VIDEO_FRAME_INFO_S frame = {};
      const int ret = CVI_VPSS_GetChnFrame(kVpssGroup, kDetectChannel, &frame, 2000);
      if (ret != CVI_SUCCESS) {
        continue;
      }

      const std::uint64_t now_ms = unix_time_ms();
      try {
        DetectionPublishResult result = run_detection(frame);
        const std::vector<DetectionObject> runtime_objects = to_runtime_objects(result.objects);
        detection_tracker_->publish(runtime_objects, result.trigger_present, now_ms);
        update_draw_objects(result.objects);
        maybe_record_detection_event(runtime_objects, now_ms);
        CVI_TDL_Free(&result.objects);
      } catch (const std::exception &error) {
        runtime_state_.record_fault("inference", error.what());
      }

      CVI_VPSS_ReleaseChnFrame(kVpssGroup, kDetectChannel, &frame);

      if (window_start_ms == 0) {
        window_start_ms = now_ms;
      }
      frames++;
      if (now_ms - window_start_ms >= 1000) {
        detection_fps_.store(frames);
        runtime_state_.set_pipeline_counters(detection_fps_.load(), encode_fps_.load());
        frames = 0;
        window_start_ms = now_ms;
      }
    }
  }

  void run_encode_loop() {
    std::uint32_t frames = 0;
    std::uint64_t window_start_ms = 0;

    while (!stop_requested_.load()) {
      VIDEO_FRAME_INFO_S frame = {};
      const int ret = CVI_VPSS_GetChnFrame(kVpssGroup, kStreamChannel, &frame, 2000);
      if (ret != CVI_SUCCESS) {
        continue;
      }

      const std::uint64_t now_ms = unix_time_ms();
      try {
        bool trigger_recording = detection_tracker_->is_present(now_ms, kDetectionStaleMs);
        cvtdl_object_t draw_objects = snapshot_draw_objects();
        if (draw_objects.size > 0) {
          rescale_detection_boxes_for_display(&draw_objects, runtime_config_.detect_width,
                                              runtime_config_.detect_height, &frame);
          const int draw_ret =
              CVI_TDL_Service_ObjectDrawRect(tdl_service_handle_, &draw_objects, &frame,
                                             runtime_config_.draw_text,
                                             CVI_TDL_Service_GetDefaultBrush());
          if (draw_ret != CVI_SUCCESS) {
            runtime_state_.record_fault("inference",
                                        format_cvi_error("CVI_TDL_Service_ObjectDrawRect", draw_ret));
          }
        }
        CVI_TDL_Free(&draw_objects);

        const RecordingEngineSnapshot before = recording_engine_->snapshot();
        send_encoded_frame(frame, trigger_recording, now_ms);
        const RecordingEngineSnapshot after = recording_engine_->snapshot();
        maybe_record_recording_event(before, after, now_ms);
      } catch (const std::exception &error) {
        runtime_state_.record_fault("media", error.what());
      }

      CVI_VPSS_ReleaseChnFrame(kVpssGroup, kStreamChannel, &frame);

      if (window_start_ms == 0) {
        window_start_ms = now_ms;
      }
      frames++;
      if (now_ms - window_start_ms >= 1000) {
        encode_fps_.store(frames);
        runtime_state_.set_pipeline_counters(detection_fps_.load(), encode_fps_.load());
        frames = 0;
        window_start_ms = now_ms;
      }
    }
  }

  DetectionPublishResult run_detection(VIDEO_FRAME_INFO_S &frame) {
    DetectionPublishResult result;
    throw_on_cvi_error("CVI_TDL_Detection",
                       CVI_TDL_Detection(tdl_handle_, &frame, model_id_, &result.objects));

    if (model_id_ == CVI_TDL_SUPPORTED_MODEL_PERSON_PETS_DETECTION) {
      result.trigger_present = filter_person_pet_objects(result.objects);
    } else {
      result.trigger_present = has_person_detection(result.objects);
    }
    return result;
  }

  bool has_person_detection(const cvtdl_object_t &objects) const {
    for (uint32_t index = 0; index < objects.size; ++index) {
      if (object_is_person(person_class_id_, objects.info[index])) {
        return true;
      }
    }
    return false;
  }

  bool filter_person_pet_objects(cvtdl_object_t &objects) const {
    bool has_person = false;
    uint32_t write_index = 0;

    for (uint32_t read_index = 0; read_index < objects.size; ++read_index) {
      cvtdl_object_info_t *object_info = &objects.info[read_index];
      if (!object_is_person_pet(*object_info)) {
        CVI_TDL_FreeObjectInfo(object_info);
        std::memset(object_info, 0, sizeof(*object_info));
        continue;
      }

      if (object_is_person(person_class_id_, *object_info)) {
        has_person = true;
      }

      std::snprintf(object_info->name, sizeof(object_info->name), "%s %.2f",
                    person_pet_label(object_info->classes), object_info->bbox.score);
      if (write_index != read_index) {
        objects.info[write_index] = *object_info;
        std::memset(object_info, 0, sizeof(*object_info));
      }
      write_index++;
    }

    objects.size = write_index;
    return has_person;
  }

  std::vector<DetectionObject> to_runtime_objects(const cvtdl_object_t &objects) const {
    std::vector<DetectionObject> runtime_objects;
    runtime_objects.reserve(objects.size);
    for (uint32_t index = 0; index < objects.size; ++index) {
      const cvtdl_object_info_t &object_info = objects.info[index];
      DetectionObject object;
      object.label = object_info.name;
      object.score = object_info.bbox.score;
      object.x1 = object_info.bbox.x1;
      object.y1 = object_info.bbox.y1;
      object.x2 = object_info.bbox.x2;
      object.y2 = object_info.bbox.y2;
      runtime_objects.push_back(std::move(object));
    }
    return runtime_objects;
  }

  void update_draw_objects(const cvtdl_object_t &objects) {
    std::lock_guard<std::mutex> lock(draw_objects_mutex_);
    CVI_TDL_Free(&draw_objects_);
    std::memset(&draw_objects_, 0, sizeof(draw_objects_));
    if (objects.size > 0) {
      CVI_TDL_CopyObjectMeta(&objects, &draw_objects_);
    }
  }

  cvtdl_object_t snapshot_draw_objects() const {
    std::lock_guard<std::mutex> lock(draw_objects_mutex_);
    cvtdl_object_t snapshot = {};
    if (draw_objects_.size > 0) {
      CVI_TDL_CopyObjectMeta(&draw_objects_, &snapshot);
    }
    return snapshot;
  }

  void maybe_record_detection_event(const std::vector<DetectionObject> &objects,
                                    std::uint64_t now_ms) {
    const bool present = detection_tracker_->is_present(now_ms, kDetectionStaleMs);
    if (present == last_detection_present_) {
      return;
    }

    runtime_state_.record_detection_event(present, objects, now_ms);
    last_detection_present_ = present;
  }

  void maybe_record_recording_event(const RecordingEngineSnapshot &before,
                                    const RecordingEngineSnapshot &after,
                                    std::uint64_t now_ms) {
    if (!before.recording && after.recording) {
      runtime_state_.record_recording_event(after.current_path, true, now_ms);
      return;
    }
    if (before.recording && !after.recording) {
      runtime_state_.record_recording_event(before.current_path, false, now_ms);
      return;
    }
    if (before.recording && after.recording && before.current_path != after.current_path) {
      runtime_state_.record_recording_event(before.current_path, false, now_ms);
      runtime_state_.record_recording_event(after.current_path, true, now_ms);
    }
  }

  void send_encoded_frame(VIDEO_FRAME_INFO_S &frame, bool trigger_recording, std::uint64_t now_ms) {
    throw_on_cvi_error("CVI_VENC_SendFrame", CVI_VENC_SendFrame(kVencChannel, &frame, 20000));

    VENC_CHN_STATUS_S status = {};
    throw_on_cvi_error("CVI_VENC_QueryStatus", CVI_VENC_QueryStatus(kVencChannel, &status));
    if (status.u32CurPacks == 0) {
      return;
    }

    std::vector<VENC_PACK_S> packs(status.u32CurPacks);
    VENC_STREAM_S stream = {};
    stream.pstPack = packs.data();
    throw_on_cvi_error("CVI_VENC_GetStream", CVI_VENC_GetStream(kVencChannel, &stream, 10000));

    try {
      EncodedFrameView frame_view;
      frame_view.chunks.reserve(stream.u32PackCount);

      CVI_RTSP_DATA rtsp_data = {};
      rtsp_data.blockCnt = stream.u32PackCount;
      rtsp_data.timestamp = stream.u32PackCount > 0 ? stream.pstPack[0].u64PTS : 0;

      if (stream.u32PackCount > CVI_RTSP_DATA_MAX_BLOCK) {
        throw std::runtime_error("VENC pack count exceeds RTSP block limit");
      }

      for (uint32_t index = 0; index < stream.u32PackCount; ++index) {
        VENC_PACK_S &pack = stream.pstPack[index];
        EncodedChunkView chunk;
        chunk.data = pack.pu8Addr + pack.u32Offset;
        chunk.size = pack.u32Len - pack.u32Offset;
        frame_view.chunks.push_back(chunk);

        rtsp_data.dataPtr[index] = const_cast<uint8_t *>(chunk.data);
        rtsp_data.dataLen[index] = static_cast<uint32_t>(chunk.size);
      }

      recording_engine_->consume_frame(frame_view, trigger_recording, now_ms);
      throw_on_rtsp_error("CVI_RTSP_WriteFrame",
                          CVI_RTSP_WriteFrame(rtsp_ctx_, rtsp_session_->video, &rtsp_data));
    } catch (...) {
      CVI_VENC_ReleaseStream(kVencChannel, &stream);
      throw;
    }

    throw_on_cvi_error("CVI_VENC_ReleaseStream", CVI_VENC_ReleaseStream(kVencChannel, &stream));
  }

  RuntimeState &runtime_state_;
  RuntimeConfig runtime_config_;
  SensorConfig sensor_config_;
  const SensorProfile *sensor_profile_ = nullptr;

  std::unique_ptr<DetectionTracker> detection_tracker_;
  std::unique_ptr<RecordingEngine> recording_engine_;

  std::atomic<bool> stop_requested_{false};
  std::thread isp_thread_;
  std::thread detection_thread_;
  std::thread encode_thread_;

  cvitdl_handle_t tdl_handle_ = nullptr;
  cvitdl_service_handle_t tdl_service_handle_ = nullptr;
  CVI_TDL_SUPPORTED_MODEL_E model_id_ = CVI_TDL_SUPPORTED_MODEL_YOLOX;
  int person_class_id_ = 0;

  mutable std::mutex draw_objects_mutex_;
  cvtdl_object_t draw_objects_ = {};

  std::atomic<std::uint32_t> detection_fps_{0};
  std::atomic<std::uint32_t> encode_fps_{0};
  bool last_detection_present_ = false;

  CVI_RTSP_CTX *rtsp_ctx_ = nullptr;
  CVI_RTSP_SESSION *rtsp_session_ = nullptr;

  bool started_ = false;
  bool sys_initialized_ = false;
  bool vi_opened_ = false;
  bool ae_awb_registered_ = false;
  bool sensor_registered_ = false;
  bool vi_device_enabled_ = false;
  bool vi_pipe_created_ = false;
  bool vi_pipe_started_ = false;
  bool isp_initialized_ = false;
  bool vi_channel_enabled_ = false;
  bool vpss_group_created_ = false;
  bool vpss_group_started_ = false;
  bool stream_channel_enabled_ = false;
  bool detect_channel_enabled_ = false;
  bool stream_pool_attached_ = false;
  bool detect_pool_attached_ = false;
  bool venc_created_ = false;
  bool venc_started_ = false;
  bool rtsp_started_ = false;
  bool tdl_handle_created_ = false;
  bool tdl_service_created_ = false;
};

}  // namespace

std::unique_ptr<CoreService::Backend> make_core_backend(RuntimeState &runtime_state) {
  return std::make_unique<CviBackend>(runtime_state);
}

}  // namespace seccam::core
