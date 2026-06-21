use axum::{extract::State, response::IntoResponse, routing::get, Json, Router};

use crate::AppState;

use super::application_error_response;

pub fn router() -> Router<AppState> {
    Router::new().route("/status", get(get_status))
}

async fn get_status(State(state): State<AppState>) -> impl IntoResponse {
    match state.services().status().await {
        Ok(status) => Json(status).into_response(),
        Err(err) => application_error_response(err).into_response(),
    }
}
