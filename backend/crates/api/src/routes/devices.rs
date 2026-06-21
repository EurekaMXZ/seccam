use axum::{extract::State, response::IntoResponse, routing::get, Json, Router};

use crate::AppState;
use super::{application_error_response, ApiListResponse};

pub fn router() -> Router<AppState> {
    Router::new().route("/devices", get(list_devices))
}

async fn list_devices(State(state): State<AppState>) -> impl IntoResponse {
    match state.services().list_devices().await {
        Ok(devices) => Json(ApiListResponse { items: devices }).into_response(),
        Err(err) => application_error_response(err).into_response(),
    }
}
