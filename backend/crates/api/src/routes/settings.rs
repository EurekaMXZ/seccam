use axum::{
    extract::State,
    response::IntoResponse,
    routing::{get, patch},
    Json, Router,
};
use seccam_domain::RuntimeConfigPatch;

use crate::AppState;
use super::application_error_response;

pub fn router() -> Router<AppState> {
    Router::new()
        .route("/settings", get(get_settings))
        .route("/settings", patch(update_settings))
}

async fn get_settings(State(state): State<AppState>) -> impl IntoResponse {
    match state.services().get_settings().await {
        Ok(settings) => Json(settings).into_response(),
        Err(err) => application_error_response(err).into_response(),
    }
}

async fn update_settings(
    State(state): State<AppState>,
    Json(patch): Json<RuntimeConfigPatch>,
) -> impl IntoResponse {
    match state.services().update_settings(patch).await {
        Ok(settings) => Json(settings).into_response(),
        Err(err) => application_error_response(err).into_response(),
    }
}
