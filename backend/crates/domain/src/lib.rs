mod error;
mod models;
mod ports;

pub use error::DomainError;
pub use models::{
    CoreStatusSnapshot, DetectionEventDetail, DetectionObject, DeviceStatus, DeviceSummary,
    EventSummary, EventType, FaultEventDetail, HealthSnapshot, LiveStatusSnapshot,
    RecordingEventDetail, RecordingFileSummary, RuntimeConfigPatch, RuntimeConfigSnapshot, ServiceHealth,
};
pub use ports::{DeviceIndex, IndexStore};
