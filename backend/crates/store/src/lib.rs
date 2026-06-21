use std::{
    fs,
    path::{Path, PathBuf},
};

use async_trait::async_trait;
use rusqlite::{params, Connection};
use seccam_domain::{DeviceIndex, DeviceStatus, DeviceSummary, DomainError};
use thiserror::Error;
use tokio::task;

#[derive(Debug, Clone)]
pub struct SqliteIndexConfig {
    pub database_path: PathBuf,
}

impl SqliteIndexConfig {
    pub fn new(database_path: impl Into<PathBuf>) -> Self {
        Self {
            database_path: database_path.into(),
        }
    }

    pub fn database_path(&self) -> &Path {
        &self.database_path
    }
}

#[derive(Debug, Clone)]
pub struct SqliteIndexStore {
    config: SqliteIndexConfig,
}

impl SqliteIndexStore {
    pub fn new(config: SqliteIndexConfig) -> Self {
        Self { config }
    }

    pub fn config(&self) -> &SqliteIndexConfig {
        &self.config
    }

    pub async fn connect(&self) -> Result<(), StoreError> {
        let path = self.config.database_path.clone();
        task::spawn_blocking(move || {
            if let Some(parent) = path.parent() {
                fs::create_dir_all(parent)
                    .map_err(|err| StoreError::Unavailable(format!("create database dir failed: {err}")))?;
            }

            let conn = Connection::open(&path)
                .map_err(|err| StoreError::Unavailable(format!("open sqlite failed: {err}")))?;
            conn.execute_batch(
                "PRAGMA journal_mode = WAL;
                 PRAGMA foreign_keys = ON;
                 CREATE TABLE IF NOT EXISTS devices (
                   id TEXT PRIMARY KEY,
                   name TEXT NOT NULL,
                   status TEXT NOT NULL,
                   stream_url TEXT,
                   last_seen_unix_ms INTEGER,
                   created_at_ms INTEGER NOT NULL,
                   updated_at_ms INTEGER NOT NULL
                 );",
            )
            .map_err(|err| StoreError::Unavailable(format!("initialize sqlite schema failed: {err}")))?;
            Ok(())
        })
        .await
        .map_err(|err| StoreError::Unavailable(format!("sqlite init task failed: {err}")))?
    }

    pub async fn upsert_device(&self, device: &DeviceSummary) -> Result<(), StoreError> {
        let path = self.config.database_path.clone();
        let device = device.clone();
        task::spawn_blocking(move || {
            let conn = Connection::open(&path)
                .map_err(|err| StoreError::Unavailable(format!("open sqlite failed: {err}")))?;
            let now_ms = current_unix_ms();
            conn.execute(
                "INSERT INTO devices (
                   id, name, status, stream_url, last_seen_unix_ms, created_at_ms, updated_at_ms
                 ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?6)
                 ON CONFLICT(id) DO UPDATE SET
                   name = excluded.name,
                   status = excluded.status,
                   stream_url = excluded.stream_url,
                   last_seen_unix_ms = excluded.last_seen_unix_ms,
                   updated_at_ms = excluded.updated_at_ms",
                params![
                    device.id,
                    device.name,
                    device_status_to_str(&device.status),
                    device.stream_url,
                    device.last_seen_unix_ms.map(|value| value as i64),
                    now_ms as i64,
                ],
            )
            .map_err(|err| StoreError::Unavailable(format!("upsert device failed: {err}")))?;
            Ok(())
        })
        .await
        .map_err(|err| StoreError::Unavailable(format!("sqlite upsert task failed: {err}")))?
    }
}

#[async_trait]
impl DeviceIndex for SqliteIndexStore {
    async fn list_devices(&self) -> Result<Vec<DeviceSummary>, DomainError> {
        let path = self.config.database_path.clone();
        task::spawn_blocking(move || {
            let conn = Connection::open(&path)
                .map_err(|err| DomainError::Unavailable(format!("open sqlite failed: {err}")))?;
            let mut statement = conn
                .prepare(
                    "SELECT id, name, status, stream_url, last_seen_unix_ms
                     FROM devices
                     ORDER BY updated_at_ms DESC, created_at_ms ASC, id ASC",
                )
                .map_err(|err| DomainError::Unavailable(format!("prepare device query failed: {err}")))?;

            let rows = statement
                .query_map([], |row| {
                    let status: String = row.get(2)?;
                    let last_seen: Option<i64> = row.get(4)?;
                    Ok(DeviceSummary {
                        id: row.get(0)?,
                        name: row.get(1)?,
                        status: device_status_from_str(&status),
                        stream_url: row.get(3)?,
                        last_seen_unix_ms: last_seen.map(|value| value as u64),
                    })
                })
                .map_err(|err| DomainError::Unavailable(format!("query devices failed: {err}")))?;

            let mut devices = Vec::new();
            for row in rows {
                devices.push(
                    row.map_err(|err| DomainError::Unavailable(format!("decode device row failed: {err}")))?,
                );
            }
            Ok(devices)
        })
        .await
        .map_err(|err| DomainError::Unavailable(format!("sqlite list task failed: {err}")))?
    }
}

fn device_status_to_str(status: &DeviceStatus) -> &'static str {
    match status {
        DeviceStatus::Online => "online",
        DeviceStatus::Starting => "starting",
        DeviceStatus::Degraded => "degraded",
        DeviceStatus::Offline => "offline",
    }
}

fn device_status_from_str(status: &str) -> DeviceStatus {
    match status {
        "online" => DeviceStatus::Online,
        "starting" => DeviceStatus::Starting,
        "degraded" => DeviceStatus::Degraded,
        _ => DeviceStatus::Offline,
    }
}

fn current_unix_ms() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

#[derive(Debug, Error)]
pub enum StoreError {
    #[error("index store unavailable: {0}")]
    Unavailable(String),
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn sqlite_store_persists_device_rows() {
        let db_path = std::env::temp_dir().join(format!(
            "seccam-store-{}.sqlite3",
            current_unix_ms()
        ));
        let store = SqliteIndexStore::new(SqliteIndexConfig::new(&db_path));
        store.connect().await.unwrap();
        store
            .upsert_device(&DeviceSummary {
                id: "dev-1".to_string(),
                name: "Board".to_string(),
                status: DeviceStatus::Offline,
                stream_url: None,
                last_seen_unix_ms: None,
            })
            .await
            .unwrap();

        let devices = store.list_devices().await.unwrap();
        assert_eq!(devices.len(), 1);
        assert_eq!(devices[0].id, "dev-1");
        assert_eq!(devices[0].name, "Board");

        let _ = fs::remove_file(db_path);
    }
}
