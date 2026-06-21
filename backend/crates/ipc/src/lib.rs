use std::path::{Path, PathBuf};

use async_trait::async_trait;
use serde::de::DeserializeOwned;
use serde_json::{json, Value};
use thiserror::Error;
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::UnixStream,
};

use seccam_domain::{
    CoreStatusSnapshot, EventSummary, RecordingFileSummary, RuntimeConfigPatch,
    RuntimeConfigSnapshot,
};

#[derive(Debug, Error)]
pub enum IpcError {
    #[error("core IPC is unavailable: {0}")]
    Unavailable(String),
    #[error("core rejected request with code {code}: {message}")]
    Rejected { code: u32, message: String },
    #[error("core IPC protocol error: {0}")]
    Protocol(String),
}

#[async_trait]
pub trait CoreClient: Send + Sync {
    async fn get_status(&self) -> Result<CoreStatusSnapshot, IpcError>;
    async fn get_config(&self) -> Result<RuntimeConfigSnapshot, IpcError>;
    async fn set_config(&self, patch: &RuntimeConfigPatch) -> Result<RuntimeConfigSnapshot, IpcError>;
    async fn list_recordings(
        &self,
        limit: u32,
        newer_than_ms: u64,
    ) -> Result<Vec<RecordingFileSummary>, IpcError>;
    async fn list_events(&self, limit: u32) -> Result<Vec<EventSummary>, IpcError>;
}

#[derive(Debug, Clone)]
pub struct UdsProtoClient {
    socket_path: PathBuf,
}

impl UdsProtoClient {
    pub fn new(socket_path: impl Into<PathBuf>) -> Self {
        Self {
            socket_path: socket_path.into(),
        }
    }

    pub fn socket_path(&self) -> &Path {
        &self.socket_path
    }

    async fn send_request(&self, payload: Value) -> Result<Value, IpcError> {
        let mut stream = UnixStream::connect(&self.socket_path)
            .await
            .map_err(|err| IpcError::Unavailable(format!("{}: {}", self.socket_path.display(), err)))?;

        let payload_bytes = serde_json::to_vec(&payload)
            .map_err(|err| IpcError::Protocol(format!("encode request failed: {err}")))?;
        let payload_len = u32::try_from(payload_bytes.len())
            .map_err(|_| IpcError::Protocol("request payload is too large".to_string()))?;

        stream
            .write_u32(payload_len)
            .await
            .map_err(|err| IpcError::Unavailable(format!("write request header failed: {err}")))?;
        stream
            .write_all(&payload_bytes)
            .await
            .map_err(|err| IpcError::Unavailable(format!("write request body failed: {err}")))?;

        let response_len = stream
            .read_u32()
            .await
            .map_err(|err| IpcError::Unavailable(format!("read response header failed: {err}")))?;
        let mut response_bytes = vec![0; response_len as usize];
        stream
            .read_exact(&mut response_bytes)
            .await
            .map_err(|err| IpcError::Unavailable(format!("read response body failed: {err}")))?;

        serde_json::from_slice(&response_bytes)
            .map_err(|err| IpcError::Protocol(format!("decode response failed: {err}")))
    }

    fn reply_error(reply: &Value) -> Option<(u32, String)> {
        let error_reply = reply.get("error_reply")?;
        let code = error_reply.get("code").and_then(Value::as_u64)? as u32;
        let message = error_reply.get("message").and_then(Value::as_str)?.to_owned();
        Some((code, message))
    }

    fn decode_value<T: DeserializeOwned>(reply: &Value, path: &[&str]) -> Result<T, IpcError> {
        let mut current = reply;
        for part in path {
            current = current.get(*part).ok_or_else(|| {
                IpcError::Protocol(format!("missing response field {}", path.join(".")))
            })?;
        }
        serde_json::from_value(current.clone())
            .map_err(|err| IpcError::Protocol(format!("decode {} failed: {err}", path.join("."))))
    }

    fn compact_object(value: &mut Value) {
        match value {
            Value::Object(map) => {
                map.retain(|_, nested| {
                    Self::compact_object(nested);
                    !nested.is_null()
                });
            }
            Value::Array(items) => {
                for item in items {
                    Self::compact_object(item);
                }
            }
            _ => {}
        }
    }
}

#[async_trait]
impl CoreClient for UdsProtoClient {
    async fn get_status(&self) -> Result<CoreStatusSnapshot, IpcError> {
        let reply = self
            .send_request(json!({
                "request_id": 1_u64,
                "status_request": {},
            }))
            .await?;
        if let Some((code, message)) = Self::reply_error(&reply) {
            return Err(IpcError::Rejected { code, message });
        }
        Self::decode_value(&reply, &["status_reply", "status"])
    }

    async fn get_config(&self) -> Result<RuntimeConfigSnapshot, IpcError> {
        let reply = self
            .send_request(json!({
                "request_id": 2_u64,
                "config_request": {"get": {}},
            }))
            .await?;
        if let Some((code, message)) = Self::reply_error(&reply) {
            return Err(IpcError::Rejected { code, message });
        }
        Self::decode_value(&reply, &["config_reply", "current"])
    }

    async fn set_config(
        &self,
        patch: &RuntimeConfigPatch,
    ) -> Result<RuntimeConfigSnapshot, IpcError> {
        let mut config_payload = serde_json::to_value(patch)
            .map_err(|err| IpcError::Protocol(format!("encode config patch failed: {err}")))?;
        Self::compact_object(&mut config_payload);
        let reply = self
            .send_request(json!({
                "request_id": 3_u64,
                "config_request": {"set": {"config": config_payload}},
            }))
            .await?;
        if let Some((code, message)) = Self::reply_error(&reply) {
            return Err(IpcError::Rejected { code, message });
        }
        Self::decode_value(&reply, &["config_reply", "current"])
    }

    async fn list_recordings(
        &self,
        limit: u32,
        newer_than_ms: u64,
    ) -> Result<Vec<RecordingFileSummary>, IpcError> {
        let reply = self
            .send_request(json!({
                "request_id": 4_u64,
                "recording_list_request": {
                    "limit": limit,
                    "newer_than_ms": newer_than_ms,
                },
            }))
            .await?;
        if let Some((code, message)) = Self::reply_error(&reply) {
            return Err(IpcError::Rejected { code, message });
        }
        Self::decode_value(&reply, &["recording_list_reply", "files"])
    }

    async fn list_events(&self, limit: u32) -> Result<Vec<EventSummary>, IpcError> {
        let reply = self
            .send_request(json!({
                "request_id": 5_u64,
                "event_list_request": {
                    "limit": limit,
                },
            }))
            .await?;
        if let Some((code, message)) = Self::reply_error(&reply) {
            return Err(IpcError::Rejected { code, message });
        }
        Self::decode_value(&reply, &["event_list_reply", "events"])
    }
}
