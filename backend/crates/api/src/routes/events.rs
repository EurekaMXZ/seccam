use axum::{
    extract::{Query, State},
    response::IntoResponse,
    routing::get,
    Json, Router,
};
use serde::Deserialize;

use crate::AppState;
use super::{application_error_response, ApiListResponse};

pub fn router() -> Router<AppState> {
    Router::new().route("/events", get(list_events))
}

#[derive(Debug, Deserialize)]
struct EventListQuery {
    limit: Option<u32>,
}

async fn list_events(
    State(state): State<AppState>,
    Query(query): Query<EventListQuery>,
) -> impl IntoResponse {
    match state.services().list_events(query.limit.unwrap_or(50)).await {
        Ok(events) => Json(ApiListResponse { items: events }).into_response(),
        Err(err) => application_error_response(err).into_response(),
    }
}
