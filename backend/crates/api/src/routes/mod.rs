mod devices;
mod events;
mod health;
mod recordings;
mod settings;
mod status;
mod ws;

use axum::{http::StatusCode, response::IntoResponse, Json, Router};
use serde::Serialize;
use seccam_application::ApplicationError;

use crate::AppState;

pub fn health_routes() -> Router<AppState> {
    health::router()
}

pub fn status_ws_routes() -> Router<AppState> {
    ws::status_router()
}

pub fn v1_routes() -> Router<AppState> {
    Router::new()
        .merge(status::router())
        .merge(devices::router())
        .merge(events::router())
        .merge(recordings::router())
        .merge(settings::router())
}

#[derive(Debug, Serialize)]
struct ApiListResponse<T> {
    items: T,
}

#[derive(Debug, Serialize)]
struct ApiErrorResponse {
    code: Option<u32>,
    error: String,
}

fn application_error_response(err: ApplicationError) -> impl IntoResponse {
    let (status, code) = match &err {
        ApplicationError::Domain(seccam_domain::DomainError::NotFound(_)) => (StatusCode::NOT_FOUND, None),
        ApplicationError::Domain(seccam_domain::DomainError::Unavailable(_)) => {
            (StatusCode::SERVICE_UNAVAILABLE, None)
        }
        ApplicationError::Ipc(seccam_ipc::IpcError::Unavailable(_)) => {
            (StatusCode::SERVICE_UNAVAILABLE, None)
        }
        ApplicationError::Ipc(seccam_ipc::IpcError::Rejected { code, .. }) => (
            StatusCode::from_u16((*code).min(u16::MAX as u32) as u16)
                .unwrap_or(StatusCode::BAD_GATEWAY),
            Some(*code),
        ),
        ApplicationError::Ipc(seccam_ipc::IpcError::Protocol(_)) => (StatusCode::BAD_GATEWAY, None),
    };

    (
        status,
        Json(ApiErrorResponse {
            code,
            error: err.to_string(),
        }),
    )
}
