use std::{env, net::SocketAddr, sync::Arc};
use std::path::PathBuf;

use anyhow::{Context, Result};
use seccam_api::{build_router, AppState};
use seccam_application::AppServices;
use seccam_ipc::UdsProtoClient;
use seccam_domain::{DeviceStatus, DeviceSummary};
use seccam_store::{SqliteIndexConfig, SqliteIndexStore};
use tokio::net::TcpListener;
use tracing::info;

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "info".into()),
        )
        .init();

    let listen_addr: SocketAddr = env::var("SECCAM_BACKEND_ADDR")
        .unwrap_or_else(|_| "127.0.0.1:8080".to_string())
        .parse()
        .context("failed to parse SECCAM_BACKEND_ADDR")?;
    let socket_path =
        env::var("SECCAM_CORE_SOCKET").unwrap_or_else(|_| "/var/run/seccam-core.sock".to_string());
    let device_id = env::var("SECCAM_DEVICE_ID").unwrap_or_else(|_| "duo-256m-001".to_string());
    let device_name =
        env::var("SECCAM_DEVICE_NAME").unwrap_or_else(|_| "MilkV Duo 256M".to_string());
    let rtsp_host = env::var("SECCAM_RTSP_HOST").unwrap_or_else(|_| "192.168.42.1".to_string());
    let store_path = env::var("SECCAM_STORE_PATH")
        .map(PathBuf::from)
        .unwrap_or_else(|_| default_store_path(&socket_path));

    let store = Arc::new(SqliteIndexStore::new(SqliteIndexConfig::new(store_path)));
    store.connect().await.context("failed to initialize sqlite store")?;
    store
        .upsert_device(&DeviceSummary {
            id: device_id,
            name: device_name,
            status: DeviceStatus::Offline,
            stream_url: None,
            last_seen_unix_ms: None,
        })
        .await
        .context("failed to upsert primary device")?;
    let ipc = Arc::new(UdsProtoClient::new(socket_path));
    let services = Arc::new(AppServices::new(
        store,
        ipc,
        env!("CARGO_PKG_VERSION"),
        rtsp_host,
    ));

    let app = build_router().with_state(AppState::new(services));
    let listener = TcpListener::bind(listen_addr)
        .await
        .context("failed to bind HTTP listener")?;

    info!("seccam backend listening on http://{}", listen_addr);
    axum::serve(listener, app)
        .await
        .context("axum server exited unexpectedly")?;
    Ok(())
}

fn default_store_path(socket_path: &str) -> PathBuf {
    let socket_path = PathBuf::from(socket_path);
    let parent = socket_path
        .parent()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."));
    parent.join("seccam-backend.sqlite3")
}
