use axum::{extract::State, routing::get, Json, Router};

use crate::AppState;

pub fn router() -> Router<AppState> {
    Router::new().route("/health", get(get_health))
}

async fn get_health(State(state): State<AppState>) -> Json<seccam_domain::HealthSnapshot> {
    Json(state.services().health().await)
}
