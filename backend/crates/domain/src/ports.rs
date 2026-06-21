use async_trait::async_trait;

use crate::{DeviceSummary, DomainError};

#[async_trait]
pub trait DeviceIndex: Send + Sync {
    async fn list_devices(&self) -> Result<Vec<DeviceSummary>, DomainError>;
}

pub trait IndexStore: DeviceIndex + Send + Sync {}

impl<T> IndexStore for T where T: DeviceIndex + Send + Sync {}
