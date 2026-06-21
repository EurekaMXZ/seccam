use std::sync::Arc;

use seccam_domain::{
    CoreStatusSnapshot, DeviceStatus, DeviceSummary, DomainError, EventSummary, HealthSnapshot,
    IndexStore, LiveStatusSnapshot, RecordingFileSummary, RuntimeConfigPatch, RuntimeConfigSnapshot,
    ServiceHealth,
};
use seccam_ipc::{CoreClient, IpcError};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ApplicationError {
    #[error(transparent)]
    Domain(#[from] DomainError),
    #[error(transparent)]
    Ipc(#[from] IpcError),
}

#[derive(Clone)]
pub struct AppServices {
    store: Arc<dyn IndexStore>,
    core: Arc<dyn CoreClient>,
    version: String,
    rtsp_host: String,
}

impl AppServices {
    pub fn new(
        store: Arc<dyn IndexStore>,
        core: Arc<dyn CoreClient>,
        version: impl Into<String>,
        rtsp_host: impl Into<String>,
    ) -> Self {
        Self {
            store,
            core,
            version: version.into(),
            rtsp_host: rtsp_host.into(),
        }
    }

    pub async fn health(&self) -> HealthSnapshot {
        match self.core.get_status().await {
            Ok(status) => HealthSnapshot {
                service: "seccam-backend".to_string(),
                status: if status.media_ready && status.inference_ready {
                    ServiceHealth::Ok
                } else {
                    ServiceHealth::Degraded
                },
                version: self.version.clone(),
                detail: format!(
                    "media_ready={}, inference_ready={}, recording={}, model_name={}, detection_fps={}, encode_fps={}",
                    status.media_ready,
                    status.inference_ready,
                    status.recording,
                    status.model_name,
                    status.detection_fps,
                    status.encode_fps
                ),
            },
            Err(err) => HealthSnapshot {
                service: "seccam-backend".to_string(),
                status: ServiceHealth::Degraded,
                version: self.version.clone(),
                detail: err.to_string(),
            },
        }
    }

    pub async fn status(&self) -> Result<LiveStatusSnapshot, ApplicationError> {
        let device = self.primary_device().await?;
        let core_status = self.core.get_status().await?;
        let config = self.core.get_config().await?;
        Ok(self.compose_live_status(device, core_status, &config))
    }

    pub async fn list_devices(&self) -> Result<Vec<DeviceSummary>, ApplicationError> {
        let devices = self.store.list_devices().await?;
        let core_status = self.core.get_status().await.ok();
        let config = self.core.get_config().await.ok();
        Ok(devices
            .into_iter()
            .enumerate()
            .map(|(index, mut device)| {
                if index == 0 {
                    if let Some(status) = core_status.as_ref() {
                        device.status = derive_device_status(status);
                        device.last_seen_unix_ms = last_seen_ms(status);
                    } else {
                        device.status = DeviceStatus::Offline;
                        device.last_seen_unix_ms = None;
                    }
                    if let Some(config) = config.as_ref() {
                        device.stream_url =
                            Some(build_rtsp_url(&self.rtsp_host, &config.rtsp_stream_name));
                    }
                }
                device
            })
            .collect())
    }

    pub async fn get_settings(&self) -> Result<RuntimeConfigSnapshot, ApplicationError> {
        Ok(self.core.get_config().await?)
    }

    pub async fn update_settings(
        &self,
        patch: RuntimeConfigPatch,
    ) -> Result<RuntimeConfigSnapshot, ApplicationError> {
        Ok(self.core.set_config(&patch).await?)
    }

    pub async fn list_events(&self, limit: u32) -> Result<Vec<EventSummary>, ApplicationError> {
        Ok(self.core.list_events(limit).await?)
    }

    pub async fn list_recordings(
        &self,
        limit: u32,
        newer_than_ms: u64,
    ) -> Result<Vec<RecordingFileSummary>, ApplicationError> {
        Ok(self.core.list_recordings(limit, newer_than_ms).await?)
    }

    async fn primary_device(&self) -> Result<DeviceSummary, ApplicationError> {
        self.store
            .list_devices()
            .await?
            .into_iter()
            .next()
            .ok_or_else(|| DomainError::NotFound("primary device".to_string()).into())
    }

    fn compose_live_status(
        &self,
        device: DeviceSummary,
        core_status: CoreStatusSnapshot,
        config: &RuntimeConfigSnapshot,
    ) -> LiveStatusSnapshot {
        LiveStatusSnapshot {
            device_id: device.id,
            device_name: device.name,
            device_status: derive_device_status(&core_status),
            stream_url: Some(build_rtsp_url(&self.rtsp_host, &config.rtsp_stream_name)),
            last_seen_unix_ms: last_seen_ms(&core_status),
            core: core_status,
        }
    }
}

fn derive_device_status(core_status: &CoreStatusSnapshot) -> DeviceStatus {
    if core_status.media_ready && core_status.inference_ready {
        DeviceStatus::Online
    } else if core_status.media_ready || core_status.inference_ready {
        DeviceStatus::Starting
    } else {
        DeviceStatus::Degraded
    }
}

fn build_rtsp_url(host: &str, stream_name: &str) -> String {
    format!("rtsp://{host}/{stream_name}")
}

fn last_seen_ms(core_status: &CoreStatusSnapshot) -> Option<u64> {
    let timestamp = core_status
        .last_detection_ms
        .max(core_status.last_recording_start_ms);
    (timestamp > 0).then_some(timestamp)
}

#[cfg(test)]
mod tests {
    use super::*;
    use async_trait::async_trait;
    use seccam_domain::{DetectionObject, IndexStore};

    struct TestStore {
        devices: Vec<DeviceSummary>,
    }

    #[async_trait]
    impl seccam_domain::DeviceIndex for TestStore {
        async fn list_devices(&self) -> Result<Vec<DeviceSummary>, DomainError> {
            Ok(self.devices.clone())
        }
    }

    struct TestCore {
        status: CoreStatusSnapshot,
        config: RuntimeConfigSnapshot,
    }

    #[async_trait]
    impl CoreClient for TestCore {
        async fn get_status(&self) -> Result<CoreStatusSnapshot, IpcError> {
            Ok(self.status.clone())
        }

        async fn get_config(&self) -> Result<RuntimeConfigSnapshot, IpcError> {
            Ok(self.config.clone())
        }

        async fn set_config(
            &self,
            _patch: &RuntimeConfigPatch,
        ) -> Result<RuntimeConfigSnapshot, IpcError> {
            Ok(self.config.clone())
        }

        async fn list_recordings(
            &self,
            _limit: u32,
            _newer_than_ms: u64,
        ) -> Result<Vec<RecordingFileSummary>, IpcError> {
            Ok(Vec::new())
        }

        async fn list_events(&self, _limit: u32) -> Result<Vec<EventSummary>, IpcError> {
            Ok(Vec::new())
        }

    }

    fn status_fixture() -> CoreStatusSnapshot {
        CoreStatusSnapshot {
            media_ready: true,
            inference_ready: true,
            recording: false,
            model_name: "mobiledetv2-pedestrian".to_string(),
            stream_width: 1280,
            stream_height: 720,
            detect_width: 640,
            detect_height: 384,
            detection_fps: 12,
            encode_fps: 15,
            last_detection_ms: 100,
            last_recording_start_ms: 0,
            current_objects: vec![DetectionObject {
                label: "person".to_string(),
                score: 0.9,
                x1: 1.0,
                y1: 2.0,
                x2: 3.0,
                y2: 4.0,
            }],
        }
    }

    fn config_fixture() -> RuntimeConfigSnapshot {
        RuntimeConfigSnapshot {
            model_name: "mobiledetv2-pedestrian".to_string(),
            model_path: "/mnt/model.cvimodel".to_string(),
            record_dir: "/mnt/records".to_string(),
            sensor_config_path: "/mnt/data/sensor_cfg.ini".to_string(),
            rtsp_stream_name: "h264".to_string(),
            threshold: 0.1,
            trigger_hits: 1,
            clear_misses: 2,
            hold_seconds: 5,
            min_record_seconds: 3,
            stream_width: 1280,
            stream_height: 720,
            detect_width: 640,
            detect_height: 384,
            tdl_vb_width: 448,
            tdl_vb_height: 448,
            bitrate_kbps: 2048,
            rtsp_port: 554,
            max_record_bytes: 1,
            max_segment_bytes: 2,
            prebuffer_bytes: 3,
            person_class_id: -1,
            draw_text: true,
        }
    }

    #[tokio::test]
    async fn list_devices_reflects_live_core_state() {
        let store: Arc<dyn IndexStore> = Arc::new(TestStore {
            devices: vec![DeviceSummary {
                id: "dev-1".to_string(),
                name: "Board".to_string(),
                status: DeviceStatus::Offline,
                stream_url: None,
                last_seen_unix_ms: None,
            }],
        });
        let core = Arc::new(TestCore {
            status: status_fixture(),
            config: config_fixture(),
        });
        let services = AppServices::new(store, core, "0.1.0", "192.168.42.1");

        let devices = services.list_devices().await.unwrap();
        assert_eq!(devices.len(), 1);
        assert!(matches!(devices[0].status, DeviceStatus::Online));
        assert_eq!(devices[0].stream_url.as_deref(), Some("rtsp://192.168.42.1/h264"));
        assert!(devices[0].last_seen_unix_ms.is_some());
    }

    #[tokio::test]
    async fn status_contains_live_device_metadata() {
        let store: Arc<dyn IndexStore> = Arc::new(TestStore {
            devices: vec![DeviceSummary {
                id: "dev-1".to_string(),
                name: "Board".to_string(),
                status: DeviceStatus::Offline,
                stream_url: None,
                last_seen_unix_ms: None,
            }],
        });
        let core = Arc::new(TestCore {
            status: status_fixture(),
            config: config_fixture(),
        });
        let services = AppServices::new(store, core, "0.1.0", "192.168.42.1");

        let snapshot = services.status().await.unwrap();
        assert_eq!(snapshot.device_id, "dev-1");
        assert_eq!(snapshot.device_name, "Board");
        assert!(matches!(snapshot.device_status, DeviceStatus::Online));
        assert_eq!(snapshot.stream_url.as_deref(), Some("rtsp://192.168.42.1/h264"));
        assert_eq!(snapshot.core.current_objects.len(), 1);
    }
}
