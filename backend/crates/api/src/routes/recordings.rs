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
    Router::new().route("/recordings", get(list_recordings))
}

#[derive(Debug, Deserialize)]
struct RecordingListQuery {
    limit: Option<u32>,
    newer_than_ms: Option<u64>,
}

async fn list_recordings(
    State(state): State<AppState>,
    Query(query): Query<RecordingListQuery>,
) -> impl IntoResponse {
    match state
        .services()
        .list_recordings(query.limit.unwrap_or(50), query.newer_than_ms.unwrap_or(0))
        .await
    {
        Ok(files) => Json(ApiListResponse { items: files }).into_response(),
        Err(err) => application_error_response(err).into_response(),
    }
}
