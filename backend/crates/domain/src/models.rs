use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DeviceStatus {
    Online,
    Starting,
    Degraded,
    Offline,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DeviceSummary {
    pub id: String,
    pub name: String,
    pub status: DeviceStatus,
    pub stream_url: Option<String>,
    pub last_seen_unix_ms: Option<u64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum EventType {
    Detection,
    Recording,
    Fault,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DetectionObject {
    pub label: String,
    pub score: f32,
    pub x1: f32,
    pub y1: f32,
    pub x2: f32,
    pub y2: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CoreStatusSnapshot {
    pub media_ready: bool,
    pub inference_ready: bool,
    pub recording: bool,
    pub model_name: String,
    pub stream_width: u32,
    pub stream_height: u32,
    pub detect_width: u32,
    pub detect_height: u32,
    pub detection_fps: u32,
    pub encode_fps: u32,
    pub last_detection_ms: u64,
    pub last_recording_start_ms: u64,
    pub current_objects: Vec<DetectionObject>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LiveStatusSnapshot {
    pub device_id: String,
    pub device_name: String,
    pub device_status: DeviceStatus,
    pub stream_url: Option<String>,
    pub last_seen_unix_ms: Option<u64>,
    pub core: CoreStatusSnapshot,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RuntimeConfigSnapshot {
    pub model_name: String,
    pub model_path: String,
    pub record_dir: String,
    pub sensor_config_path: String,
    pub rtsp_stream_name: String,
    pub threshold: f32,
    pub trigger_hits: u32,
    pub clear_misses: u32,
    pub hold_seconds: u32,
    pub min_record_seconds: u32,
    pub stream_width: u32,
    pub stream_height: u32,
    pub detect_width: u32,
    pub detect_height: u32,
    pub tdl_vb_width: u32,
    pub tdl_vb_height: u32,
    pub bitrate_kbps: u32,
    pub rtsp_port: u32,
    pub max_record_bytes: u64,
    pub max_segment_bytes: u64,
    pub prebuffer_bytes: u64,
    pub person_class_id: i32,
    pub draw_text: bool,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct RuntimeConfigPatch {
    pub model_name: Option<String>,
    pub model_path: Option<String>,
    pub record_dir: Option<String>,
    pub sensor_config_path: Option<String>,
    pub rtsp_stream_name: Option<String>,
    pub threshold: Option<f32>,
    pub trigger_hits: Option<u32>,
    pub clear_misses: Option<u32>,
    pub hold_seconds: Option<u32>,
    pub min_record_seconds: Option<u32>,
    pub stream_width: Option<u32>,
    pub stream_height: Option<u32>,
    pub detect_width: Option<u32>,
    pub detect_height: Option<u32>,
    pub tdl_vb_width: Option<u32>,
    pub tdl_vb_height: Option<u32>,
    pub bitrate_kbps: Option<u32>,
    pub rtsp_port: Option<u32>,
    pub max_record_bytes: Option<u64>,
    pub max_segment_bytes: Option<u64>,
    pub prebuffer_bytes: Option<u64>,
    pub person_class_id: Option<i32>,
    pub draw_text: Option<bool>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RecordingFileSummary {
    pub path: String,
    pub size: u64,
    pub started_ms: u64,
    pub ended_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DetectionEventDetail {
    pub present: bool,
    pub objects: Vec<DetectionObject>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RecordingEventDetail {
    pub file_path: String,
    pub active: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FaultEventDetail {
    pub component: String,
    pub message: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EventSummary {
    pub event_type: EventType,
    pub happened_ms: u64,
    pub summary: String,
    pub detection: Option<DetectionEventDetail>,
    pub recording: Option<RecordingEventDetail>,
    pub fault: Option<FaultEventDetail>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ServiceHealth {
    Ok,
    Degraded,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HealthSnapshot {
    pub service: String,
    pub status: ServiceHealth,
    pub version: String,
    pub detail: String,
}
